#ifndef OPENUG2_HUD_RACE_H
#define OPENUG2_HUD_RACE_H
#include "hud.h"
#include "hud_asset.h"

enum { HUD_RACE_NONE, HUD_RACE_COUNTDOWN, HUD_RACE_RUNNING, HUD_RACE_FINISHED, HUD_RACE_FAILED };
enum { HR_POSITION, HR_ORDINAL, HR_CARS, HR_LAP_LABEL, HR_LAP, HR_LAPS,
       HR_TIME_LABEL, HR_TIME, HR_BANNER, HR_DRIFT_CHAIN, HR_DRIFT_TOTAL,
       HR_DRIFT_TOTAL_LABEL, HR_DRIFT_ANGLE,
       HR_SHIFT_GRADE,      /* M9: HUD_Drag "Perfect Shift!" slot, host caption while feedback runs */
       HR_DRIFT_MULTIPLIER, /* M9: HUD_Drift "X5" slot beside the chain score */
       HR_FIELD_COUNT };
typedef struct { int event, phase, active; double elapsed; } HudRaceClock;
const char *hud_race_package(int kind);
int hud_race_package_read(const unsigned char *data, size_t len,
                          const char *name, HaPackage *out);
void hud_race_clock_step(HudRaceClock *clock, int event, int phase, double dt);
int hud_race_time(double seconds, char out[64]);
int hud_race_text(const HudState *state, int field, char out[64]);
uint32_t hud_race_field_key(int kind, int field);

/* Project a world point into the displayed authored map (logical pixels). */
int hud_race_map_point(const HaMapFrame *frame,double x,double y,double out[2]);

float hud_race_drag_angle(const HaNode *needle,double rpm);

/* M9 pure decisions (GL-free, unit-tested in hud_race_test).
 * 3rdperson_BoxLaps: lap races only (racing, circuit, laps > 0, not drag). */
int hud_race_box_laps(const HudState *state);
/* Getaway_Distance group: 1 shown with *fraction = lead/goal clamped to 0..1
 * and text "+ N"/"- N" (whole metres, |N| <= 1e6); 0 absent; -1 have_outrun
 * with a nonfinite lead/goal or nonpositive goal (hidden, diagnostic). */
int hud_race_outrun(const HudState *state,double *fraction,char text[64]);
/* Screen-clockwise rotation (radians) of an icon for a world heading in the
 * car_heading convention (+X = 0, CCW) on a map turned by map_heading
 * (clockwise from north, as HaMapFrame.heading). 0 = icon art points up. */
double hud_race_icon_angle(double map_heading,double world_heading);

#endif
