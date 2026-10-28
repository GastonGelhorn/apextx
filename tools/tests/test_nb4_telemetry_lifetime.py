# SPDX-License-Identifier: GPL-2.0-only
"""The real telemetry start/stop functions must release their periodic timer."""
from pathlib import Path
import unittest
from test_nb4_usb_transitions import run_cpp


def body(source, name):
    begin=source.index('void '+name+'(')
    start=source.index('{',begin);depth=1;end=start+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]


class TelemetryLifetime(unittest.TestCase):
    def test_stop_before_start_is_safe_and_every_started_timer_is_stopped(self):
        source=(Path(__file__).resolve().parents[2]/'radio/src/telemetry/telemetry.cpp').read_text()
        model=r'''
struct Timer {bool created=false,active=false;} telemetryTimer;
int created=0,stopped=0;
void telemetryTimerCb(Timer*){}
bool timer_is_created(Timer* t){return t->created;}
void timer_stop(Timer* t){assert(t->created);t->active=false;++stopped;}
void timer_create(Timer* t,void(*)(Timer*),const char*,int,bool){assert(!t->created);t->created=true;++created;}
void timer_start(Timer* t){assert(t->created);t->active=true;}
void clearTelemetryRxBuffers(){}
'''
        scenario=r'''
int main(){
 telemetryStop();assert(!stopped);
 telemetryStart();assert(telemetryTimer.active && created==1);
 telemetryStop();assert(!telemetryTimer.active && stopped==1);
 telemetryStop();assert(!telemetryTimer.active && stopped==2);
 telemetryStart();assert(telemetryTimer.active && created==1);
 telemetryStop();assert(!telemetryTimer.active && stopped==3);
}
'''
        run_cpp(model+body(source,'telemetryStart')+body(source,'telemetryStop')+scenario)


if __name__=='__main__':unittest.main()
