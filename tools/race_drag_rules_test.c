#include "race_rules.h"
#include "physics.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(rr_drag_grade(6300,7000,7500,1,2)==RR_SHIFT_EARLY);
    assert(rr_drag_grade(6400,7000,7500,1,2)==RR_SHIFT_GOOD);
    assert(rr_drag_grade(6850,7000,7500,1,2)==RR_SHIFT_PERFECT);
    assert(rr_drag_grade(7150,7000,7500,1,2)==RR_SHIFT_PERFECT);
    assert(rr_drag_grade(7350,7000,7500,1,2)==RR_SHIFT_GOOD);
    assert(rr_drag_grade(7351,7000,7500,1,2)==RR_SHIFT_LATE);
    assert(rr_drag_grade(7500,7000,7500,1,2)==RR_SHIFT_LATE);
    assert(rr_drag_grade(NAN,7000,7500,1,2)==RR_SHIFT_NONE);
    assert(rr_drag_grade(7000,7000,7500,2,1)==RR_SHIFT_NONE);
    assert(rr_drag_grade(7000,7000,7500,6,7)==RR_SHIFT_NONE);
    assert(rr_drag_grade_label(RR_SHIFT_PERFECT)==0x0598b065u);
    assert(rr_drag_failure(1,0,0)==RR_FAIL_ENGINE);
    assert(rr_drag_failure(0,8,0)==RR_FAIL_NONE);
    assert(rr_drag_failure(0,8.01f,0)==RR_FAIL_CRASH);
    assert(rr_drag_failure(0,0,.01f)==RR_FAIL_CRASH);
    float heat=0;for(int k=0;k<91;k++)heat=rr_drag_heat(heat,7500,7500,1,1/60.f);
    assert(heat==1);assert(rr_drag_heat(heat,7500,7500,0,1)==.5f);
    assert(rr_drag_heat(0,7499,7500,1,10)==0);
    assert(rr_drag_heat(.4f,7500,7500,1,0)==.4f);
    N2PhysicsAttr motor={.mass_tonnes=1,.idle_rpm=800,.redline_rpm=6500,.limiter_rpm=7200};
    motor.gearbox[0]=(N2GearboxAttr){.final_drive=4,.gear_count=2,.forward={3,2}};
    for(int k=0;k<9;k++)motor.torque[k]=.2f;
    PhysManual manual={.gear=1,.shift_point_rpm=6200};
    float speed=6200*6.283185307f*.3f/(PHYS_TICKRATE*60*3*4);
    phys_manual_step(&manual,&motor,0,0,.3f,speed,1,1,1/60.f);
    assert(manual.gear==2 && manual.shift_quality==RR_SHIFT_PERFECT && manual.feedback_seconds>0);
    for(int k=0;k<61;k++)phys_manual_step(&manual,&motor,0,0,.3f,speed,1,0,1/60.f);
    assert(manual.shift_quality==RR_SHIFT_NONE && manual.feedback_seconds==0);
    manual=(PhysManual){.gear=1};speed=7300*6.283185307f*.3f/(PHYS_TICKRATE*60*3*4);
    for(int k=0;k<92;k++)phys_manual_step(&manual,&motor,0,0,.3f,speed,1,0,1/60.f);
    assert(manual.failed && manual.heat==1);
    assert(phys_manual_step(&manual,&motor,0,0,.3f,0,0,0,1)==0 && manual.failed);
    puts("race_drag_rules_test: PASS (shift windows/labels/over-rev/cooling/invalid)");
    return 0;
}
