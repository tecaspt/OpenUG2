#include "gui_dialog.h"
#include <stdio.h>
static int checks,failures;
#define CHECK(c) do{checks++;if(!(c)){failures++;printf("FAIL line %d: %s\n",__LINE__,#c);}}while(0)
int main(void) {
    const GdKind kinds[]={GD_ENGAGE_SHOP,GD_ENGAGE_BODY,GD_ENGAGE_PAINT,GD_ENGAGE_PERF,GD_ENGAGE_SPECIALTY};
    const uint32_t keys[]={0x0aab553e,0x78dad861,0x85d2ed13,0x15d6b438,0x15d6b438};
    for(int i=0;i<5;i++) {
        GdSpec spec;GuiDialog d;gd_spec(&spec,kinds[i],NULL);
        CHECK(spec.message_key==keys[i]&&spec.message&&spec.initial_focus==1);
        CHECK(spec.label_key[0]==0xf770&&spec.label_key[1]==0x61c);
        gd_open(&d,&spec);gd_input(&d,GD_IN_ACCEPT);gd_update(&d,1);
        CHECK(gd_poll(&d)==GD_RES_SECOND&&gd_poll(&d)==GD_RES_NONE);
        gd_open(&d,&spec);gd_input(&d,GD_IN_LEFT);gd_input(&d,GD_IN_ACCEPT);gd_update(&d,1);
        CHECK(gd_poll(&d)==GD_RES_FIRST);
        gd_open(&d,&spec);gd_input(&d,GD_IN_LEFT);gd_input(&d,GD_IN_BACK);gd_update(&d,1);
        CHECK(gd_poll(&d)==GD_RES_SECOND);
        gd_open(&d,&spec);CHECK(gd_pointer_click(&d,-150,74));gd_update(&d,1);
        CHECK(gd_poll(&d)==GD_RES_FIRST);
    }
    printf("career_shop_dialog_test: %d checks, %d failures\n",checks,failures);
    return failures!=0;
}
