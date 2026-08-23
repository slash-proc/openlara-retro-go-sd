#include "gnw_internal.h"

extern "C" {
#include "odroid_system.h"
#include "odroid_input.h"
#include "gnw_bridge.h"
}

#include "ol/common.h"

/*
 * GBA-style OpenLara mapping (lara.h __GNW__ branch):
 *   A       = Action (switches, grab, push/pull)
 *   B       = Jump
 *   L+A     = Weapon draw/holster
 *   L+B     = Look up/down (dive/sidestep aid)
 *   R       = Walk
 *   L+R     = Look
 *   START   = Inventory
 *
 * G&W physical buttons → those virtual keys. MENU is left for Retro-Go pause
 * (do not bind Walk to MENU — common_emu steals it).
 */
extern "C" void gnw_input_update(void)
{
    odroid_gamepad_state_t joy;
    odroid_input_read_gamepad(&joy);

    uint32_t k = 0;
    if (joy.values[ODROID_INPUT_UP])
        k |= IK_UP;
    if (joy.values[ODROID_INPUT_RIGHT])
        k |= IK_RIGHT;
    if (joy.values[ODROID_INPUT_DOWN])
        k |= IK_DOWN;
    if (joy.values[ODROID_INPUT_LEFT])
        k |= IK_LEFT;
    if (joy.values[ODROID_INPUT_A])
        k |= IK_A;
    if (joy.values[ODROID_INPUT_B])
        k |= IK_B;

    /* TIME / START / X → inventory */
    if (joy.values[ODROID_INPUT_START] || joy.values[ODROID_INPUT_X])
        k |= IK_START;

    /* VOLUME → GBA L (weapon / look modifier) */
    if (joy.values[ODROID_INPUT_VOLUME])
        k |= IK_L;

    /*
     * GAME / SELECT / Y → Walk (GBA R). Also keeps SELECT for passport UI.
     * Walk must not live on MENU: the firmware pause handler owns that key.
     */
    if (joy.values[ODROID_INPUT_SELECT] || joy.values[ODROID_INPUT_Y]) {
        k |= IK_R;
        k |= IK_SELECT;
    }

    keys = k;
}
