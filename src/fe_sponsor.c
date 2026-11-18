#include "fe_sponsor.h"
#include "fe_binding.h"
#include "fe_screen.h"
#include "ugui/ugui_core.h"
#include "ugui/ugui_face.h"
#include <stdio.h>
#include <string.h>

void fsp_open(FeSponsor *p, const CareerData *cd, const CareerState *s)
{
    memset(p, 0, sizeof *p); p->bank = s->bank;
    for (int st = 0; st < cd->nstage; st++) if (cd->stage[st].index == s->stage)
        for (int k = 0; k < cd->stage[st].nsponsors && k < 5; k++)
            for (int j = 0; j < cd->nsponsor; j++) if (p->n < 5 && cd->sponsor[j].hash == cd->stage[st].sponsor[k]) {
                int i = p->n++; p->offer[i] = cd->sponsor[j]; p->index[i] = j;
                p->eligible[i] = cr_sponsor_eligible(s, cd, j);
            }
}
int fsp_input(FeSponsor *p, int in)
{
    if (in == FES_IN_BACK) { if (p->page == 1) {p->page = 0; return FSP_PAGE;} return FSP_LEAVE; }
    if (p->page == 1 && p->n) {
        if ((in == FES_IN_LEFT || in == 5) && p->sel > 0) p->sel--;
        if ((in == FES_IN_RIGHT || in == 6) && p->sel + 1 < p->n) p->sel++;
    }
    if (in != FES_IN_ACCEPT) return FSP_NONE;
    if (p->page == 2) return FSP_LEAVE;
    if (p->page == 0) {p->page = 1; return FSP_PAGE;}
    return p->n && p->eligible[p->sel] ? FSP_SIGN : FSP_NONE;
}
const char *fsp_package(const FeSponsor *p)
{
    return p->page == 0 ? "UI_SponsorPopup.fng" : p->page == 1 ? "UI_Sponsorship_new.fng" : "UI_RewardsSponsor.fng";
}
int fsp_bind(FeSponsor *p, GvScreen *s, const UgAssets *a)
{
    const char *name = fsp_package(p);
    if (!gv_screen_load(s, a, name)) return 0;
    for (unsigned k = 0; k < sizeof fe_binding_sets / sizeof fe_binding_sets[0]; k++)
        if (!strcmp(fe_binding_sets[k].package, name)) {
            for (int j = 0; j < fe_binding_sets[k].count; j++)
                if (fe_identity_find(s->pk, &fe_binding_sets[k].identity[j]) < 0) {
                    snprintf(s->status, sizeof s->status, "sponsor identity mismatch"); return 0;
                }
            return 1;
        }
    return 0;
}
static void set_text(GvScreen *s, uint32_t hash, const char *value)
{
    int at = ug_scene_find_name(&s->scene, hash);
    if (at >= 0) {s->scene.nodes[at].text_override = value; s->scene.nodes[at].app_hidden = !value[0];}
}
int fsp_sync(FeSponsor *p, GvScreen *s)
{
    if (!s->loaded || !p->n) return 0;
    const CdSponsor *sp = &p->offer[p->sel]; const UgAssets *a = s->assets;
    for (int j = 0; j < s->scene.n; j++) {
        UgNode *n = &s->scene.nodes[j]; const char *t = n->obj->text;
        if (n->obj->is_text && t && (strstr(t, "$JOY") || strstr(t, "Event Handler") ||
            strstr(t, "WWWW") || strstr(t, "99999") || !strcmp(t, "PLAYER NAME") || strstr(t, "Descriptions"))) n->app_hidden = 1;
    }
    if (p->page == 0) return 1;
    char key[80]; snprintf(key, sizeof key, "SPONSOR_%s", sp->name);
    snprintf(p->name, sizeof p->name, "%s%s", gv_text(a, ug_hash_upper(key), sp->name),
             p->eligible[p->sel] || p->page == 2 ? "" : " (LOCKED)");
    set_text(s, 0xf13f76b4u, p->name);
    snprintf(key, sizeof key, "DECAL_ICON_%s", sp->name);
    int logo = ug_scene_find_name(&s->scene, 0xb04669e3u);
    if (logo >= 0) {
        s->scene.nodes[logo].tex_override = gv_texture(a, ug_hash_upper(key));
        s->scene.nodes[logo].app_hidden = !s->scene.nodes[logo].tex_override;
    }
    snprintf(p->signing, sizeof p->signing, "$%u", sp->signing);
    snprintf(p->bonus, sizeof p->bonus, "$%u", sp->url_bonus);
    snprintf(p->balance, sizeof p->balance, "$%lld", p->bank);
    set_text(s, 0xf25b6bbau, p->signing); set_text(s, 0x7d8e1791u, p->bonus);
    set_text(s, 0xeb3a688au, p->balance); set_text(s, 0xeb406fecu, "");
    snprintf(key, sizeof key, "SPONSOR_DETAILS_%s", sp->name);
    const char *body = gv_text(a, ug_hash_upper(key), "");
    snprintf(p->body, sizeof p->body, "%s", body);
    for (char *c = p->body; *c; c++) if (*c == '^') *c = '\n';
    /* Retail's rows are independently wrapped; render one measured block in their column. */
    for (int k = 0; k < 10; k++) {
        uint32_t hash = k == 9 ? 0x9577494au : 0xfcc58ddau + (uint32_t)k;
        set_text(s, hash, "");
    }
    snprintf(key, sizeof key, "SPONSOR_INFO_%s", sp->name);
    snprintf(p->races, sizeof p->races, "%s", gv_text(a, ug_hash_upper(key), ""));
    for (char *c = p->races; *c; c++) if (*c == '^') *c = '\n';
    set_text(s, 0xf13cef3fu, p->page == 1 ? p->races : "");
    set_text(s, 0x195d8d22u, ""); set_text(s, 0x4b7ead10u, p->page == 2 ? p->races : "");
    ug_scene_relayout(&s->scene);
    return 1;
}

void fsp_draw(FeSponsor *p, GvScreen *s, UgCanvas *cv)
{
    if (!p || !s->loaded || p->page == 0) return;
    int at = ug_scene_find_name(&s->scene, 0xfcc58ddau);
    if (at < 0) return;
    const UgFace *previous = ug_text_face();
    ug_text_set_face(s->scene.nodes[at].face);
    float scale = .58f, width = 0, height = 0;
    for (int k = 0; k < 12; k++) {
        ug_text_measure(p->body, scale, 250/scale, 0, UG_GLYPHS_MOUSE, &width, &height);
        if (height <= 173) break;
        scale *= .94f;
    }
    ug_text_draw(cv, p->body, -277, -21, scale, (UgColor){230,230,230,255}, 0, 250/scale, 0, UG_GLYPHS_MOUSE);
    ug_text_set_face(previous);
}
