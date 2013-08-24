/*
 * Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 *
 * (c) Copyright 1996 - 2001 Gary Henderson (gary.henderson@ntlworld.com) and
 *                           Jerremy Koot (jkoot@snes9x.com)
 *
 * Super FX C emulator code 
 * (c) Copyright 1997 - 1999 Ivar (ivar@snes9x.com) and
 *                           Gary Henderson.
 * Super FX assembler emulator code (c) Copyright 1998 zsKnight and _Demo_.
 *
 * DSP1 emulator code (c) Copyright 1998 Ivar, _Demo_ and Gary Henderson.
 * C4 asm and some C emulation code (c) Copyright 2000 zsKnight and _Demo_.
 * C4 C code (c) Copyright 2001 Gary Henderson (gary.henderson@ntlworld.com).
 *
 * DOS port code contains the works of other authors. See headers in
 * individual files.
 *
 * Snes9x homepage: http://www.snes9x.com
 *
 * Permission to use, copy, modify and distribute Snes9x in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Snes9x is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for Snes9x or software derived from Snes9x.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Super NES and Super Nintendo Entertainment System are trademarks of
 * Nintendo Co., Limited and its subsidiary companies.
 */
#ifdef __DJGPP
//#include <allegro.h>
#undef TRUE
#endif

#include "snes9x.h"
#include "spc700.h"
#include "apu.h"
/*#include "cpuexec.h"*/
#include "port.h"
#include "soundSystem.hpp"

extern int NoiseFreq [32];
extern SoundSystem *sndSys;

#ifdef DEBUGGER
void S9xTraceSoundDSP (const char *s, int i1 = 0, int i2 = 0, int i3 = 0,
		       int i4 = 0, int i5 = 0, int i6 = 0, int i7 = 0);
#endif

bool8_32 S9xInitAPU ()
{
    IAPU.RAM = (uint8 *) malloc (0x10000);
    IAPU.ShadowRAM = (uint8 *) malloc (0x10000);
    IAPU.CachedSamples = (uint8 *) malloc (0x40000);
    
    if (!IAPU.RAM || !IAPU.ShadowRAM || !IAPU.CachedSamples)
    {
	S9xDeinitAPU ();
	return (FALSE);
    }

    return (TRUE);
}

void S9xDeinitAPU ()
{
    if (IAPU.RAM)
    {
	free ((char *) IAPU.RAM);
	IAPU.RAM = NULL;
    }
    if (IAPU.ShadowRAM)
    {
	free ((char *) IAPU.ShadowRAM);
	IAPU.ShadowRAM = NULL;
    }
    if (IAPU.CachedSamples)
    {
	free ((char *) IAPU.CachedSamples);
	IAPU.CachedSamples = NULL;
    }
}

EXTERN_C uint8 APUROM [64];

void S9xResetAPU ()
{
    Settings.APUEnabled = Settings.NextAPUEnabled;

    memset (IAPU.RAM, Settings.APURAMInitialValue, 0x10000);
    memset (IAPU.ShadowRAM, Settings.APURAMInitialValue, 0x10000);
    
    ZeroMemory (IAPU.CachedSamples, 0x40000);
    ZeroMemory (APU.OutPorts, 4);
    IAPU.DirectPage = IAPU.RAM;
    memmove (&IAPU.RAM [0xffc0], APUROM, sizeof (APUROM));
    memmove (APU.ExtraRAM, APUROM, sizeof (APUROM));
    IAPU.PC = IAPU.RAM + IAPU.RAM [0xfffe] + (IAPU.RAM [0xffff] << 8);
    APU.Cycles = 0;
    APURegisters.YA.W = 0;
    APURegisters.X = 0;
    APURegisters.S = 0xff;
    APURegisters.P = 0;
    S9xAPUUnpackStatus ();
    APURegisters.PC = 0;
    IAPU.APUExecuting = Settings.APUEnabled;
#ifdef SPC700_SHUTDOWN
    IAPU.WaitAddress1 = NULL;
    IAPU.WaitAddress2 = NULL;
    IAPU.WaitCounter = 0;
#endif
    APU.ShowROM = TRUE;
    IAPU.RAM [0xf1] = 0x80;

    int i;

    for (i = 0; i < 3; i++)
    {
	APU.TimerEnabled [i] = FALSE;
	APU.TimerValueWritten [i] = 0;
	APU.TimerTarget [i] = 0;
	APU.Timer [i] = 0;
    }
    for (int j = 0; j < 0x80; j++)
	APU.DSP [j] = 0;

    IAPU.TwoCycles = IAPU.OneCycle * 2;

    for (i = 0; i < 256; i++)
	S9xAPUCycles [i] = S9xAPUCycleLengths [i] * IAPU.OneCycle;

    APU.DSP [APU_ENDX] = 0;
    APU.DSP [APU_KOFF] = 0;
    APU.DSP [APU_KON] = 0;
    APU.DSP [APU_FLG] = APU_MUTE | APU_ECHO_DISABLED;
    APU.KeyedChannels = 0;

    sndSys->reset(true);
    sndSys->setEchoEnable(0);
}

void S9xSetAPUDSP (uint8 byte, struct SAPU *apu, struct SIAPU *iapu)
{
    uint8 reg = iapu->RAM [0xf2];

    switch (reg)
    {
    case APU_FLG:
	if (byte & APU_SOFT_RESET)
	{
	    apu->DSP [reg] = APU_MUTE | APU_ECHO_DISABLED | (byte & 0x1f);
	    apu->DSP [APU_ENDX] = 0;
	    apu->DSP [APU_KOFF] = 0;
	    apu->DSP [APU_KON] = 0;
	    sndSys->setEchoWriteEnable(false);
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] DSP reset\n", ICPU.Scanline);
#endif
	    // Kill sound
	    sndSys->reset(false);
	}
	else
	{
	    sndSys->setEchoWriteEnable(!(byte & APU_ECHO_DISABLED));
	    if (byte & APU_MUTE)
	    {
#ifdef DEBUGGER
		    if (Settings.TraceSoundDSP)
			    S9xTraceSoundDSP ("[%d] Mute sound\n", ICPU.Scanline);
#endif
		    sndSys->setMute(true);
	    }
	    else
		    sndSys->setMute(false);

	    sndSys->setAllChannelsNoiseFreq(NoiseFreq [byte & 0x1f]);
	}
	break;
    case APU_NON:
	if (byte != apu->DSP [APU_NON])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Noise:", ICPU.Scanline);
#endif
	    uint8 mask = 1;
	    for (int c = 0; c < 8; c++, mask <<= 1)
	    {
		SoundType type;
		if (byte & mask)
		{
		    type = SOUND_NOISE;
#ifdef DEBUGGER
		    if (Settings.TraceSoundDSP)
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d,", c);
			else
			    S9xTraceSoundDSP ("%d(on),", c);
		    }
#endif
		}
		else
		{
		    type = SOUND_SAMPLE;
#ifdef DEBUGGER
		    if (Settings.TraceSoundDSP)
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d(off),", c);
		    }
#endif
		}
		sndSys->setChannelType(c, type);
	    }
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("\n");
#endif
	}
	break;
    case APU_MVOL_LEFT:
	if (byte != apu->DSP [APU_MVOL_LEFT])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Master volume left:%d\n", 
				  ICPU.Scanline, (signed char) byte);
#endif
	    sndSys->setVolume(byte, apu->DSP[APU_MVOL_RIGHT]);
	}
	break;
    case APU_MVOL_RIGHT:
	if (byte != apu->DSP [APU_MVOL_RIGHT])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Master volume right:%d\n",
				  ICPU.Scanline, (signed char) byte);
#endif
	    sndSys->setVolume(apu->DSP[APU_MVOL_LEFT], byte);
	}
	break;
    case APU_EVOL_LEFT:
	if (byte != apu->DSP [APU_EVOL_LEFT])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Echo volume left:%d\n",
				  ICPU.Scanline, (signed char) byte);
#endif
	    sndSys->setEchoVolume(byte, apu->DSP[APU_EVOL_RIGHT]);
	}
	break;
    case APU_EVOL_RIGHT:
	if (byte != apu->DSP [APU_EVOL_RIGHT])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Echo volume right:%d\n",
				  ICPU.Scanline, (signed char) byte);
#endif
	    sndSys->setEchoVolume(apu->DSP[APU_EVOL_LEFT], byte);
	}
	break;
    case APU_ENDX:
#ifdef DEBUGGER
	if (Settings.TraceSoundDSP)
	    S9xTraceSoundDSP ("[%d] Reset ENDX\n", ICPU.Scanline);
#endif
	byte = 0;
	break;

    case APU_KOFF:
	if (byte)
	{
	    uint8 mask = 1;
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Key off:", ICPU.Scanline);
#endif
	    for (int c = 0; c < 8; c++, mask <<= 1)
	    {
		if ((byte & mask) != 0)
		{
#ifdef DEBUGGER

		    if (Settings.TraceSoundDSP)
			S9xTraceSoundDSP ("%d,", c);
#endif		    
		    if (apu->KeyedChannels & mask)
		    {
			{
			    apu->KeyedChannels &= ~mask;
			    apu->DSP [APU_KON] &= ~mask;
			    //apu->DSP [APU_KOFF] |= mask;
			    sndSys->setSoundKeyOff(c);
			}
		    }
		}
	    }
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("\n");
#endif
	}
	apu->DSP [APU_KOFF] = byte;
	return;
    case APU_KON:
	if (byte)
	{
	    uint8 mask = 1;
#ifdef DEBUGGER

	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] Key on:", ICPU.Scanline);
#endif
	    for (int c = 0; c < 8; c++, mask <<= 1)
	    {
		if ((byte & mask) != 0)
		{
#ifdef DEBUGGER
		    if (Settings.TraceSoundDSP)
			S9xTraceSoundDSP ("%d,", c);
#endif		    
		    // Pac-In-Time requires that channels can be key-on
		    // regardeless of their current state.
		    apu->KeyedChannels |= mask;
		    apu->DSP [APU_KON] |= mask;
		    apu->DSP [APU_KOFF] &= ~mask;
		    apu->DSP [APU_ENDX] &= ~mask;
		    sndSys->playSample (c, apu);
		}
	    }
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("\n");
#endif
	}
	return;
	
    case APU_VOL_LEFT + 0x00:
    case APU_VOL_LEFT + 0x10:
    case APU_VOL_LEFT + 0x20:
    case APU_VOL_LEFT + 0x30:
    case APU_VOL_LEFT + 0x40:
    case APU_VOL_LEFT + 0x50:
    case APU_VOL_LEFT + 0x60:
    case APU_VOL_LEFT + 0x70:
// At Shin Megami Tensei suggestion 6/11/00
//	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d volume left: %d\n", 
				  ICPU.Scanline, reg>>4, (signed char) byte);
#endif
//		S9xSetSoundVolume (reg >> 4, (signed char) byte,
//				   (signed char) apu->DSP [reg + 1]);
		int ch = reg >> 4;
		sndSys->setChannelVolume(ch, byte, apu->DSP[reg + 1]);
	}
	break;
    case APU_VOL_RIGHT + 0x00:
    case APU_VOL_RIGHT + 0x10:
    case APU_VOL_RIGHT + 0x20:
    case APU_VOL_RIGHT + 0x30:
    case APU_VOL_RIGHT + 0x40:
    case APU_VOL_RIGHT + 0x50:
    case APU_VOL_RIGHT + 0x60:
    case APU_VOL_RIGHT + 0x70:
// At Shin Megami Tensei suggestion 6/11/00
//	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d volume right: %d\n", 
				  ICPU.Scanline, reg >>4, (signed char) byte);
#endif
	    
//		S9xSetSoundVolume (reg >> 4, (signed char) apu->DSP [reg - 1],
//				   (signed char) byte);
		int ch = reg >> 4;
		sndSys->setChannelVolume(ch, apu->DSP[reg - 1], byte);
	}
	break;

    case APU_P_LOW + 0x00:
    case APU_P_LOW + 0x10:
    case APU_P_LOW + 0x20:
    case APU_P_LOW + 0x30:
    case APU_P_LOW + 0x40:
    case APU_P_LOW + 0x50:
    case APU_P_LOW + 0x60:
    case APU_P_LOW + 0x70:
#ifdef DEBUGGER
	if (Settings.TraceSoundDSP)
	    S9xTraceSoundDSP ("[%d] %d freq low: %d\n",
			      ICPU.Scanline, reg>>4, byte);
#endif
//	    S9xSetSoundHertz (reg >> 4, ((byte + (apu->DSP [reg + 1] << 8)) & FREQUENCY_MASK) * 8);
		{
			int ch = reg >> 4;
			int hertz = ((byte + (apu->DSP [reg + 1] << 8)) & FREQUENCY_MASK) * 8;
			sndSys->setChannelFrequency(ch, hertz);
			sndSys->setSoundFrequency(ch, hertz);
			break;
		}
    case APU_P_HIGH + 0x00:
    case APU_P_HIGH + 0x10:
    case APU_P_HIGH + 0x20:
    case APU_P_HIGH + 0x30:
    case APU_P_HIGH + 0x40:
    case APU_P_HIGH + 0x50:
    case APU_P_HIGH + 0x60:
    case APU_P_HIGH + 0x70:
#ifdef DEBUGGER
	if (Settings.TraceSoundDSP)
	    S9xTraceSoundDSP ("[%d] %d freq high: %d\n",
			      ICPU.Scanline, reg>>4, byte);
#endif
//	    S9xSetSoundHertz (reg >> 4, 
//			      (((byte << 8) + apu->DSP [reg - 1]) & FREQUENCY_MASK) * 8);
		{
			int ch = reg >> 4;
			int hertz = (((byte << 8) + apu->DSP [reg - 1]) & FREQUENCY_MASK) * 8;
			sndSys->setChannelFrequency(ch, hertz);
			sndSys->setSoundFrequency(ch, hertz);
		}
	break;

    case APU_SRCN + 0x00:
    case APU_SRCN + 0x10:
    case APU_SRCN + 0x20:
    case APU_SRCN + 0x30:
    case APU_SRCN + 0x40:
    case APU_SRCN + 0x50:
    case APU_SRCN + 0x60:
    case APU_SRCN + 0x70:
	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d sample number: %d\n",
				  ICPU.Scanline, reg>>4, byte);
#endif
	}
	break;
	
    case APU_ADSR1 + 0x00:
    case APU_ADSR1 + 0x10:
    case APU_ADSR1 + 0x20:
    case APU_ADSR1 + 0x30:
    case APU_ADSR1 + 0x40:
    case APU_ADSR1 + 0x50:
    case APU_ADSR1 + 0x60:
    case APU_ADSR1 + 0x70:
	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d adsr1: %02x\n",
				  ICPU.Scanline, reg>>4, byte);
#endif
	    {
		sndSys->fixEnvelope(reg >> 4, apu->DSP [reg + 2], byte,
		                    apu->DSP [reg + 1]);
	    }
	}
	break;

    case APU_ADSR2 + 0x00:
    case APU_ADSR2 + 0x10:
    case APU_ADSR2 + 0x20:
    case APU_ADSR2 + 0x30:
    case APU_ADSR2 + 0x40:
    case APU_ADSR2 + 0x50:
    case APU_ADSR2 + 0x60:
    case APU_ADSR2 + 0x70:
	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d adsr2: %02x\n", 
				  ICPU.Scanline, reg>>4, byte);
#endif
	    {
		    sndSys->fixEnvelope(reg >> 4, apu->DSP [reg + 1], apu->DSP [reg - 1],
		                        byte);
	    }
	}
	break;

    case APU_GAIN + 0x00:
    case APU_GAIN + 0x10:
    case APU_GAIN + 0x20:
    case APU_GAIN + 0x30:
    case APU_GAIN + 0x40:
    case APU_GAIN + 0x50:
    case APU_GAIN + 0x60:
    case APU_GAIN + 0x70:
	if (byte != apu->DSP [reg])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
		S9xTraceSoundDSP ("[%d] %d gain: %02x\n",
				  ICPU.Scanline, reg>>4, byte);
#endif
	    {
		    sndSys->fixEnvelope (reg >> 4, byte, apu->DSP [reg - 2],
		                         apu->DSP [reg - 1]);
	    }
	}
	break;

    case APU_ENVX + 0x00:
    case APU_ENVX + 0x10:
    case APU_ENVX + 0x20:
    case APU_ENVX + 0x30:
    case APU_ENVX + 0x40:
    case APU_ENVX + 0x50:
    case APU_ENVX + 0x60:
    case APU_ENVX + 0x70:
	break;

    case APU_OUTX + 0x00:
    case APU_OUTX + 0x10:
    case APU_OUTX + 0x20:
    case APU_OUTX + 0x30:
    case APU_OUTX + 0x40:
    case APU_OUTX + 0x50:
    case APU_OUTX + 0x60:
    case APU_OUTX + 0x70:
	break;
    
    case APU_DIR:
#ifdef DEBUGGER
	if (Settings.TraceSoundDSP)
	    S9xTraceSoundDSP ("[%d] Sample directory to: %02x\n",
			      ICPU.Scanline, byte);
#endif
	break;

    case APU_PMON:
	if (byte != apu->DSP [APU_PMON])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
	    {
		S9xTraceSoundDSP ("[%d] FreqMod:", ICPU.Scanline);
		uint8 mask = 1;
		for (int c = 0; c < 8; c++, mask <<= 1)
		{
		    if (byte & mask)
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d", c);
			else
			    S9xTraceSoundDSP ("%d(on),", c);
		    }
		    else
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d(off),", c);
		    }
		}
		S9xTraceSoundDSP ("\n");
	    }
#endif
		sndSys->setFrequencyModulationEnable(byte);
	}
	break;

    case APU_EON:
	if (byte != apu->DSP [APU_EON])
	{
#ifdef DEBUGGER
	    if (Settings.TraceSoundDSP)
	    {
		S9xTraceSoundDSP ("[%d] Echo:", ICPU.Scanline);
		uint8 mask = 1;
		for (int c = 0; c < 8; c++, mask <<= 1)
		{
		    if (byte & mask)
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d", c);
			else
			    S9xTraceSoundDSP ("%d(on),", c);
		    }
		    else
		    {
			if (apu->DSP [reg] & mask)
			    S9xTraceSoundDSP ("%d(off),", c);
		    }
		}
		S9xTraceSoundDSP ("\n");
	    }
#endif
		sndSys->setEchoEnable(byte);
	}
	break;

    case APU_EFB:
	    sndSys->setEchoFeedback(byte);
	    break;

    case APU_ESA:
	break;

    case APU_EDL:
	    sndSys->setEchoDelay(byte & 0xf);
	    break;

    case APU_C0:
    case APU_C1:
    case APU_C2:
    case APU_C3:
    case APU_C4:
    case APU_C5:
    case APU_C6:
    case APU_C7:
	    sndSys->setFilterCoefficient (reg >> 4, (signed char) byte);
	break;
    default:
// XXX
//printf ("Write %02x to unknown APU register %02x\n", byte, reg);
	break;
    }

    if (reg < 0x80)
	apu->DSP [reg] = byte;
}



void S9xSetAPUControl (uint8 byte)
{
//if (byte & 0x40)
//printf ("*** Special SPC700 timing enabled\n");
    if ((byte & 1) != 0 && !APU.TimerEnabled [0])
    {
	APU.Timer [0] = 0;
	IAPU.RAM [0xfd] = 0;
	if ((APU.TimerTarget [0] = IAPU.RAM [0xfa]) == 0)
	    APU.TimerTarget [0] = 0x100;
    }
    if ((byte & 2) != 0 && !APU.TimerEnabled [1])
    {
	APU.Timer [1] = 0;
	IAPU.RAM [0xfe] = 0;
	if ((APU.TimerTarget [1] = IAPU.RAM [0xfb]) == 0)
	    APU.TimerTarget [1] = 0x100;
    }
    if ((byte & 4) != 0 && !APU.TimerEnabled [2])
    {
	APU.Timer [2] = 0;
	IAPU.RAM [0xff] = 0;
	if ((APU.TimerTarget [2] = IAPU.RAM [0xfc]) == 0)
	    APU.TimerTarget [2] = 0x100;
    }
    APU.TimerEnabled [0] = byte & 1;
    APU.TimerEnabled [1] = (byte & 2) >> 1;
    APU.TimerEnabled [2] = (byte & 4) >> 2;

    if (byte & 0x10)
	IAPU.RAM [0xF4] = IAPU.RAM [0xF5] = 0;

    if (byte & 0x20)
	IAPU.RAM [0xF6] = IAPU.RAM [0xF7] = 0;

    if (byte & 0x80)
    {
	if (!APU.ShowROM)
	{
	    memmove (&IAPU.RAM [0xffc0], APUROM, sizeof (APUROM));
	    APU.ShowROM = TRUE;
	}
    }
    else
    {
	if (APU.ShowROM)
	{
	    APU.ShowROM = FALSE;
	    memmove (&IAPU.RAM [0xffc0], APU.ExtraRAM, sizeof (APUROM));
	}
    }
    IAPU.RAM [0xf1] = byte;
}

void S9xSetAPUTimer (uint16 Address, uint8 byte)
{
    IAPU.RAM [Address] = byte;

    switch (Address)
    {
    case 0xfa:
	if ((APU.TimerTarget [0] = IAPU.RAM [0xfa]) == 0)
	    APU.TimerTarget [0] = 0x100;
	APU.TimerValueWritten [0] = TRUE;
	break;
    case 0xfb:
	if ((APU.TimerTarget [1] = IAPU.RAM [0xfb]) == 0)
	    APU.TimerTarget [1] = 0x100;
	APU.TimerValueWritten [1] = TRUE;
	break;
    case 0xfc:
	if ((APU.TimerTarget [2] = IAPU.RAM [0xfc]) == 0)
	    APU.TimerTarget [2] = 0x100;
	APU.TimerValueWritten [2] = TRUE;
	break;
    }
}

uint8 S9xGetAPUDSP ()
{
    uint8 reg = IAPU.RAM [0xf2] & 0x7f;
    uint8 byte = APU.DSP [reg];

    switch (reg)
    {
    case APU_KON:
	break;
    case APU_KOFF:
	break;
    case APU_OUTX + 0x00:
    case APU_OUTX + 0x10:
    case APU_OUTX + 0x20:
    case APU_OUTX + 0x30:
    case APU_OUTX + 0x40:
    case APU_OUTX + 0x50:
    case APU_OUTX + 0x60:
    case APU_OUTX + 0x70:
	    if (sndSys->getChannelState(reg >> 4) == SOUND_SILENT)
		    return (0);
	    return ((sndSys->getChannelSample(reg >> 4) >> 8) |
	            (sndSys->getChannelSample(reg >> 4) & 0xff));

    case APU_ENVX + 0x00:
    case APU_ENVX + 0x10:
    case APU_ENVX + 0x20:
    case APU_ENVX + 0x30:
    case APU_ENVX + 0x40:
    case APU_ENVX + 0x50:
    case APU_ENVX + 0x60:
    case APU_ENVX + 0x70:
	return sndSys->getEnvelopeHeight(reg >> 4);

    case APU_ENDX:
// To fix speech in Magical Drop 2 6/11/00
//	APU.DSP [APU_ENDX] = 0;
	break;
    default:
	break;
    }
    return (byte);
}
