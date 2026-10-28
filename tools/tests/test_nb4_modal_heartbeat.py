# SPDX-License-Identifier: GPL-2.0-only
"""Responsive warning dialogs must not be diagnosed as a frozen UI."""
from pathlib import Path
import unittest
from test_nb4_usb_transitions import run_cpp


class ModalHeartbeat(unittest.TestCase):
    def test_real_nested_loop_reports_activity_after_rendering_and_still_exposes_hangs(self):
        path=Path(__file__).resolve().parents[2]/'radio/src/gui/colorlcd/libui/fullscreen_dialog.cpp'
        source=path.read_text();start=source.index('static void run_ui_manually()')
        code=source[start:source.index('void FullScreenDialog::runForever',start)]
        model=r'''
#define RADIO_NB4_FAMILY
#define WDG_RESET() (void)0
enum {NB4_TASK_UI};
unsigned now=0,lastBeat=0;bool nested=false,rendered=false,hang=false,detected=false;
void checkBacklight(){}
void sleep_ms(unsigned ms){now+=ms;}
bool stalled(){return now-lastBeat>5000;}
void nb4HealthBeat(int task){assert(task==NB4_TASK_UI && nested && rendered);lastBeat=now;}
struct LvglWrapper {static void runNested(){nested=true;rendered=false;}};
struct MainWindow {
 static MainWindow* instance(){static MainWindow w;return &w;}
 void run(bool){assert(nested);if(hang){now+=6000;detected=stalled();}rendered=true;}
};
'''
        scenario=r'''
int main(){
 for(unsigned i=0;i<800;++i){run_ui_manually();assert(!stalled());}
 assert(now>=8000); // A user may leave a startup warning open indefinitely.
 hang=true;run_ui_manually();assert(detected); // No heartbeat before a blocked render.
}
'''
        run_cpp(model+code+scenario)


if __name__=='__main__':unittest.main()
