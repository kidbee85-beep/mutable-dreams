/* SPDX-License-Identifier: MPL-2.0 */
/* include/midi_rel.h: both relative codes, nothing for 64 and 0, a value held at its ends */
#include <stdio.h>
#include "../include/midi_rel.h"

static int fails;
static void check(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %s: got %d, want %d\n", what, got, want);
        fails++;
    }
}
int main(void)
{
    check("65 is +1", midi_rel_delta(65), 1);
    check("63 is -1", midi_rel_delta(63), -1);
    check("72 is +8", midi_rel_delta(72), 8);
    check("56 is -8", midi_rel_delta(56), -8);
    check("64 is nothing", midi_rel_delta(64), 0);
    check("1 is +1", midi_rel_delta(1), 1);
    check("127 is -1", midi_rel_delta(127), -1);
    check("5 is +5", midi_rel_delta(5), 5);
    check("120 is -8", midi_rel_delta(120), -8);
    check("0 is nothing", midi_rel_delta(0), 0);
    check("a value turned", midi_rel_apply(60, 65, 0, 127), 61);
    check("held at the top", midi_rel_apply(127, 1, 0, 127), 127);
    check("held at the bottom", midi_rel_apply(0, 127, 0, 127), 0);
    check("a small range", midi_rel_apply(2, 66, 0, 3), 3);
    if (!fails)
        printf("midi_rel: ok\n");
    return fails != 0;
}
