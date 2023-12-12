/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-23 divingkatae and maximum
                      (theweirdo)     spatium

(Contact divingkatae#1017 or powermax#2286 on Discord for more info)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <core/hostevents.h>
#include <loguru.hpp>
#include <emscripten.h>

EventManager* EventManager::event_manager;

void EventManager::poll_events()
{
    int lock = EM_ASM_INT_V({ return workerApi.acquireInputLock(); });
    if (!lock) {
        return;
    }

    int mouse_button_state = EM_ASM_INT_V({
        return workerApi.getInputValue(workerApi.InputBufferAddresses.mouseButtonStateAddr);
    });
    if (mouse_button_state > -1) {
        MouseEvent me;
        me.buttons_state = mouse_button_state;
        me.flags         = MOUSE_EVENT_BUTTON;
        this->_mouse_signal.emit(me);
    }

    int has_mouse_position = EM_ASM_INT_V({
        return workerApi.getInputValue(workerApi.InputBufferAddresses.mousePositionFlagAddr);
    });
    if (has_mouse_position) {
        int delta_x = EM_ASM_INT_V({
            return workerApi.getInputValue(workerApi.InputBufferAddresses.mouseDeltaXAddr);
        });
        int delta_y = EM_ASM_INT_V({
            return workerApi.getInputValue(workerApi.InputBufferAddresses.mouseDeltaYAddr);
        });

        MouseEvent me;
        me.xrel  = delta_x;
        me.yrel  = delta_y;
        me.flags = MOUSE_EVENT_MOTION;
        this->_mouse_signal.emit(me);
    }

    int has_key_event = EM_ASM_INT_V({
        return workerApi.getInputValue(workerApi.InputBufferAddresses.keyEventFlagAddr);
    });
    if (has_key_event) {
        int keycode = EM_ASM_INT_V({
            return workerApi.getInputValue(workerApi.InputBufferAddresses.keyCodeAddr);
        });

        int keystate = EM_ASM_INT_V({
            return workerApi.getInputValue(workerApi.InputBufferAddresses.keyStateAddr);
        });

        KeyboardEvent ke;
        ke.key       = keycode;
        ke.flags     = keystate == 0 ? KEYBOARD_EVENT_UP : KEYBOARD_EVENT_DOWN;
        this->_keyboard_signal.emit(ke);
    }

    EM_ASM({ workerApi.releaseInputLock(); });

    // Ensure that period tasks are run (until we have idlewait support).
    EM_ASM({ workerApi.sleep(0); });

    // perform post-processing
    this->_post_signal.emit();
}
