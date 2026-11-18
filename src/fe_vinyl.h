/* Career vinyl shop: copied snapshot, pure navigation and the retail layer-page binding.
 * docs/parity/VINYLS.md */
#ifndef OPENUG2_FE_VINYL_H
#define OPENUG2_FE_VINYL_H
#include "career_data.h"
#include "career.h"
#include "gui_view.h"

#define FEV_MAX   512   /* design catalogue of one car; every VINYLS.BIN slot of a car fits */
#define FEV_CATS   32   /* one tab per CR_VINYL_CAT_* the car's own VINYLS.BIN can fill */
#define FEV_ROWS   10   /* designs shown at once in the carousel */

/* One design of the active car's own catalogue (VINYLS.BIN token + its retail category). */
typedef struct FeVinylDesign {
    char     token[24];      /* VINYLS.BIN record name, upper case, e.g. FLAME_005 */
    int      category;       /* CR_VINYL_CAT_* */
    int      colors;         /* 1 (artFactory house style) or CR_VINYL_COLORS */
    int      price;          /* the shop's price for this design, 0 when unreadable */
    int      open;           /* the category is unlocked for this career */
    int      owned;          /* already installed on this layer of the active car */
} FeVinylDesign;

/* A VINYL-group paint colour that can be bought for a colour channel. */
typedef struct FeVinylColor { CdPaint paint; int index, price, open; } FeVinylColor;

typedef struct FeVinylShop {
    long long bank;
    FeVinylDesign design[FEV_MAX]; int ndesign;
    FeVinylColor color[CD_MAX_PAINT]; int ncolor;
    /* the layer being edited and what it already holds (tokens resolved by the host) */
    int layer;
    char current[24];
    uint32_t current_colour[CR_VINYL_COLORS];
} FeVinylShop;

typedef enum { FVT_MSG_NONE, FVT_MSG_CONFIRM, FVT_MSG_NO_FUNDS, FVT_MSG_LOCKED,
               FVT_MSG_OWNED, FVT_MSG_SAVE, FVT_MSG_REMOVED } FvtMsg;
typedef enum { FVT_EV_NONE = 0, FVT_EV_LEAVE = 1, FVT_EV_BUY = 2, FVT_EV_REMOVE = 3,
               FVT_EV_PAGE = 4 } FvtEvent;

/* Screen inputs. The first four are the shared FesInput values (fe_screen.h); the vinyl
 * flow also pages its category tabs, which retail drives with the shoulder triggers. */
enum { FVT_IN_PREV_TAB = 10, FVT_IN_NEXT_TAB = 11 };

/* Page of the vinyl flow. The layer list is the retail CHOOSE_VINYL_LAYER screen; the
   design list is the category carousel; the colour page is PAINT_CHOOSE_VINYL_COLORS. */
typedef enum { FVT_PAGE_LAYER = 0, FVT_PAGE_DESIGN = 1, FVT_PAGE_COLOR = 2 } FvtPage;

/* A buy request. `remove` clears the layer; otherwise `token` plus the chosen colour keys
   are what the career layer transaction takes. */
typedef struct FeVinylRequest {
    int  layer;
    char token[24];
    uint32_t colour[CR_VINYL_COLORS];
    int  remove;
} FeVinylRequest;

typedef struct FeVinyl {
    FeVinylShop shop;
    int page, cats[FEV_CATS], ncat, cat_sel;
    int rows[FEV_MAX], nrows, first, sel, colour_sel[CR_VINYL_COLORS], confirm;
    FeVinylRequest req;
    FvtMsg msg;
    char caption[192], cash[48], total[32], label[96], mark[96], status[160];
} FeVinyl;

/* What the car behind the screen should wear while browsing: the layer being edited and
 * the design + colours chosen for it (empty token = this screen previews nothing). */
void fvt_preview(const FeVinyl *, char *token, int cap, uint32_t colour[CR_VINYL_COLORS]);
int fvt_preview_layer(const FeVinyl *);

void fvt_open(FeVinyl *, const FeVinylShop *);
void fvt_refresh(FeVinyl *, const FeVinylShop *, FvtMsg);
/* After a completed transaction: keeps the request the host is still reading but leaves the
 * screen on the layer row, so nothing stays previewed on the car. docs/parity/VINYLS.md */
void fvt_settle(FeVinyl *);
FvtEvent fvt_input(FeVinyl *, int);
const FeVinylDesign *fvt_selected(const FeVinyl *);
const char *fvt_package(const FeVinyl *);
int fvt_bind(FeVinyl *, GvScreen *, const UgAssets *);
int fvt_sync(FeVinyl *, GvScreen *, float);
#endif
