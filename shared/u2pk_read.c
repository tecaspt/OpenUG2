/* U2PK v1 reader - see docs/U2PK.md.
 *
 * The runtime hands the GE pointers straight out of this buffer, so every
 * offset below is proved in range before it is trusted, and every rule only
 * looks at bytes an earlier rule validated. Returns 1 on acceptance; on
 * rejection view->err names the first rule that failed and nothing in the view
 * should be read.
 */
#include <math.h>
#include <string.h>
#include "u2pk.h"

/* Bytes each element of a section occupies, or 0 for types with no element
 * size to check (M4 formats). */
static uint32_t sect_stride(uint32_t type)
{
	switch (type) {
	case U2PK_S_BATCHES:  return (uint32_t)sizeof(U2pkBatch);
	case U2PK_S_VERTS:    return (uint32_t)sizeof(U2pkVertex);
	case U2PK_S_INDICES:  return sizeof(uint16_t);
	case U2PK_S_TEXTURES: return (uint32_t)sizeof(U2pkTexHeader);
	case U2PK_S_CLUTS:    return 256u * sizeof(uint32_t);
	case U2PK_S_CARINFO:  return (uint32_t)sizeof(U2pkCarInfo);
	default:              return 0;
	}
}

static int float_ok(float f)
{
	return isfinite(f) && f > 0.0f;
}

static int ranges_overlap(uint64_t s1, uint64_t e1, uint64_t s2, uint64_t e2)
{
	return s1 < e2 && s2 < e1;
}

static const uint8_t *at(const U2pkView *v, uint64_t off, uint64_t n, const char **err)
{
	if (off + n > v->len) {
		*err = "section runs past end of file";
		return 0;
	}
	return v->buf + off;
}

int u2pk_open(const uint8_t *buf, uint32_t len, U2pkView *out)
{
	U2pkView v;
	const U2pkSection *sect;
	uint32_t i, k;
	uint64_t need;
	const char *bad = 0;
	int have[U2PK_MAX_SECTIONS + 1];

	memset(out, 0, sizeof *out);
	memset(&v, 0, sizeof v);
	if (!buf || !out || len < (uint32_t)sizeof(U2pkHeader)) {
		out->err = "file shorter than header";
		return 0;
	}
	v.buf = buf;
	v.len = len;
	if (memcmp(buf, U2PK_MAGIC, 4) != 0) {
		out->err = "bad magic";
		return 0;
	}

	memcpy(&v.hdr, buf, sizeof v.hdr);
	if (v.hdr.version != U2PK_VERSION) {
		out->err = "unsupported version";
		return 0;
	}
	if (v.hdr.total_size != len) {
		out->err = "total_size does not match file length";
		return 0;
	}
	if (v.hdr.kind != U2PK_KIND_CAR && v.hdr.kind != U2PK_KIND_REGION &&
	    v.hdr.kind != U2PK_KIND_CELL) {
		out->err = "unknown kind";
		return 0;
	}

	need = (uint64_t)sizeof(U2pkHeader) +
	       (uint64_t)v.hdr.section_count * sizeof(U2pkSection);
	if (v.hdr.section_count > U2PK_MAX_SECTIONS || need > len) {
		out->err = "section table runs past end of file";
		return 0;
	}
	sect = (const U2pkSection *)(buf + sizeof(U2pkHeader));
	for (i = 0; i < v.hdr.section_count; i++) {
		if (sect[i].type == 0 || sect[i].type > U2PK_S_CELLTAB) {
			out->err = "unknown section type";
			return 0;
		}
	}
	v.sect = sect;
	for (k = 0; k <= U2PK_MAX_SECTIONS; k++)
		have[k] = 0;

	for (i = 0; i < v.hdr.section_count; i++) {
		uint32_t type = sect[i].type;
		uint32_t stride = sect_stride(type);
		const uint8_t *p;

		if (have[type]) {
			out->err = "duplicate section";
			return 0;
		}
		if (sect[i].offset % U2PK_ALIGN) {
			out->err = "section offset not 16-byte aligned";
			return 0;
		}
		if (stride) {
			uint64_t bytes = (uint64_t)sect[i].count * stride;
			if (bytes > sect[i].size) {
				out->err = "section count x element size exceeds its size";
				return 0;
			}
		}
		p = at(&v, sect[i].offset, sect[i].size, &bad);
		if (!p) {
			out->err = bad;
			return 0;
		}
		have[type] = 1;
		switch (type) {
		case U2PK_S_BATCHES:
			v.batch = (const U2pkBatch *)p;
			v.nbatch = sect[i].count;
			break;
		case U2PK_S_VERTS:
			v.vert = (const U2pkVertex *)p;
			v.nvert = sect[i].count;
			break;
		case U2PK_S_INDICES:
			v.index = (const uint16_t *)p;
			v.nindex = sect[i].count;
			break;
		case U2PK_S_TEXTURES:
			v.tex = (const U2pkTexHeader *)p;
			v.ntex = sect[i].count;
			break;
		case U2PK_S_CLUTS:
			v.clut = (const uint32_t *)p;
			v.nclut = sect[i].count;
			break;
		case U2PK_S_CARINFO:
			v.car = (const U2pkCarInfo *)p;
			break;
		default:
			break;   /* M4 sections: range-checked only */
		}
	}

	if (v.hdr.kind == U2PK_KIND_CAR && (!v.batch || !v.vert || !v.index || !v.car)) {
		out->err = "car file missing a required section";
		return 0;
	}

	if (v.car) {
		const U2pkCarInfo *c = v.car;
		const float *wp = &c->wheel_pos[0][0];
		uint64_t a0 = (uint64_t)c->batch_first[0] + c->batch_count[0];
		uint64_t a1 = (uint64_t)c->batch_first[1] + c->batch_count[1];
		uint64_t aw = (uint64_t)c->wheel_batch_first + c->wheel_batch_count;
		if (c->lod_count != U2PK_LODS) {
			out->err = "lod_count must be 2";
			return 0;
		}
		if (a0 > v.nbatch || a1 > v.nbatch || aw > v.nbatch) {
			out->err = "car batch range outside BATCHES";
			return 0;
		}
		/* LOD0, LOD1 and the wheel run must not overlap: the runtime draws
		 * them as three independent ranges. */
		if (ranges_overlap(c->batch_first[0], a0, c->batch_first[1], a1) ||
		    ranges_overlap(c->batch_first[0], a0, c->wheel_batch_first, aw) ||
		    ranges_overlap(c->batch_first[1], a1, c->wheel_batch_first, aw)) {
			out->err = "car batch ranges overlap";
			return 0;
		}
		for (k = 0; k < 12; k++) {
			if (!isfinite(wp[k])) {
				out->err = "wheel_pos not finite";
				return 0;
			}
		}
		if (!isfinite(c->wheel_radius) || c->wheel_radius <= 0.0f) {
			out->err = "wheel_radius must be positive";
			return 0;
		}
		for (k = 0; k < 3; k++) {
			if (!isfinite(c->body_box[2 * k]) || !isfinite(c->body_box[2 * k + 1]) ||
			    c->body_box[2 * k] > c->body_box[2 * k + 1]) {
				out->err = "body_box is inverted or not finite";
				return 0;
			}
		}
	}

	for (i = 0; i < v.nbatch; i++) {
		const U2pkBatch *b = &v.batch[i];
		if ((uint64_t)b->first_vertex + b->vertex_count > v.nvert ||
		    (uint64_t)b->first_index + b->index_count > v.nindex) {
			out->err = "batch range outside VERTS or INDICES";
			return 0;
		}
		/* Indices are u16 and batch-local in v1, so a batch cannot own more
		 * than 65536 vertices even if the file has room for them. */
		if (b->vertex_count > 65536u || b->vertex_count == 0 ||
		    b->index_count == 0 || b->index_count % 3) {
			out->err = "batch vertex/index count is unusable";
			return 0;
		}
		/* Indices are batch-local, so the GE adding first_vertex must not be
		 * able to step past the batch's own vertex run. */
		for (k = 0; k < b->index_count; k++)
			if (v.index[b->first_index + k] >= b->vertex_count) {
				out->err = "index outside the batch's vertex run";
				return 0;
			}
		if (b->texture >= (int32_t)v.ntex) {
			/* A CELL file may carry no TEXTURES section of its own: its
			 * batches then index the shared sidecar (tex.u2pk), whose count
			 * this reader never sees. Only the runtime, which has both open,
			 * can range-check those; require the index to be a valid sidecar
			 * reference (>= 0) here. Every other kind (CAR, and a REGION or
			 * CELL that does embed textures) is still checked against its own
			 * section. */
			int sidecar = (v.hdr.kind == U2PK_KIND_CELL && v.ntex == 0);
			if (!(sidecar && b->texture >= 0)) {
				out->err = "batch texture index out of range";
				return 0;
			}
		}
		if (b->mode > U2PK_MODE_BLEND) {
			out->err = "batch mode out of range";
			return 0;
		}
		for (k = 0; k < 3; k++)
			if (!float_ok(b->pos_scale[k])) {
				out->err = "pos_scale must be finite and positive";
				return 0;
			}
		for (k = 0; k < 2; k++)
			if (!float_ok(b->uv_scale[k])) {
				out->err = "uv_scale must be finite and positive";
				return 0;
			}
	}

	for (i = 0; i < v.ntex; i++) {
		const U2pkTexHeader *t = &v.tex[i];
		if (t->data_offset % U2PK_ALIGN) {
			out->err = "texture data not 16-byte aligned";
			return 0;
		}
		if ((uint64_t)t->data_offset + t->data_size > len) {
			out->err = "texture data runs past end of file";
			return 0;
		}
		if (t->clut >= (int32_t)v.nclut) {
			out->err = "clut index out of range";
			return 0;
		}
		if (t->w == 0 || t->h == 0) {
			out->err = "texture has no size";
			return 0;
		}
		if (t->psm == U2PK_PSM_T8 && (t->w < 16 || t->h < 8)) {
			out->err = "T8 texture below the GE minimum size";
			return 0;
		}
	}

	v.err = 0;
	*out = v;
	return 1;
}
