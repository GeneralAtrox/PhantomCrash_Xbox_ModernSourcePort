"""SPU ADSR register adapter, evaluated at the PS2's 48 kHz output rate.

Shift/step rules: psx-spx SPU Volume and ADSR Generator. Hardware interpolation
and reverb are separate from these envelope registers and are not reproduced.
"""

import numpy as np

RATE = 48000


def envelope(adsr1, adsr2, held, limit):
    levels = np.empty(limit, np.float32)
    phase, level, counter, released = 0, 0, 0, False
    for i in range(limit):
        if i >= held and not released:
            phase, counter, released = 3, 0, True
        if phase == 0:
            shift, step = (adsr1 >> 10) & 31, (adsr1 >> 8) & 3
            exponential, decrease = bool(adsr1 & 0x8000), False
        elif phase == 1:
            shift, step, exponential, decrease = (adsr1 >> 4) & 15, 0, True, True
        elif phase == 2:
            shift, step = (adsr2 >> 8) & 31, (adsr2 >> 6) & 3
            exponential, decrease = bool(adsr2 & 0x8000), bool(adsr2 & 0x4000)
        else:
            shift, step, exponential, decrease = adsr2 & 31, 0, bool(adsr2 & 32), True
        increment = 32768 >> max(0, shift-11)
        change = (-(8-step) if decrease else 7-step) << max(0, 11-shift)
        if exponential and not decrease and level > 0x6000:
            if shift < 10:
                change >>= 2
            elif shift >= 11:
                increment >>= 2
            else:
                change >>= 1
                increment >>= 1
        elif exponential and decrease:
            change = (change * level) >> 15
        frozen = (phase in (0, 2) and shift == 31 and step == 3) or (phase == 3 and shift == 31)
        if not frozen:
            increment = max(increment, 1)
        counter += increment
        if counter >= 32768:
            counter &= 32767
            level = max(0, min(32767, level + change))
            if phase == 0 and level == 32767:
                phase, counter = 1, 0
            elif phase == 1 and level <= min(32767, ((adsr1 & 15)+1)*2048):
                phase, counter = 2, 0
        levels[i] = level / 32767
        if released and level == 0:
            return levels[:i+1]
    if released and level > 0:
        raise ValueError("Sound release exceeds bounded render duration")
    return levels
