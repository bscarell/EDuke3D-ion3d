//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include <time.h>
#include <stdlib.h>

#include "m32script.h"
#include "m32def.h"

#include "sounds_mapster32.h"
#include "fx_man.h"

#include "osd.h"
#include "keys.h"
#include "common.h"

#include "colmatch.h"

// from macros.h
#define rnd(X) ((krand()>>8)>=(255-(X)))

vmstate_t vm;
vmstate_t vm_default =
{
    -1,   // g_i
    0,    // g_st
    NULL, // g_sp
    0,    // flags
    0,    // miscflags
};

int32_t g_errorLineNum, g_tw;

uint8_t aEventEnabled[MAXEVENTS];

uint32_t m32_drawlinepat=0xffffffff;
int32_t m32_script_expertmode = 0;

instype *insptr;
int32_t VM_Execute(int32_t once);
static instype *x_sortingstateptr;

//#include "m32structures.c"

#ifdef DEBUGGINGAIDS
void X_Disasm(ofstype beg, int32_t size)
{
    instype *p;

    if (!script) return;
    if (beg<0 || beg+size>g_scriptSize) return;

    initprintf("beg=%d, size=%d:  ", beg, size);
    for (p=script+beg; p<script+beg+size; p++)
    {
        if (*p>>12 && (*p&0xFFF)<CON_END)
            initprintf("%s ", keyw[*p&0xFFF]);
        else
            initprintf("%d ", *p);
    }
    initprintf("\n");
}
#endif

void VM_ScriptInfo(void)
{
    if (script)
    {
        instype *p;
        if (insptr)
            for (p=max(insptr-20,script); p<min(insptr+20, script+g_scriptSize); p++)
            {
                if (p==insptr) initprintf("<<");

                if (*p>>12 && (*p&0xFFF)<CON_END)
                    initprintf("\n%5d: L%5d:  %s ",(int32_t)(p-script),(int32_t)(*p>>12),keyw[*p&0xFFF]);
                else initprintf(" %d",*p);

                if (p==insptr) initprintf(">>");
            }
        initprintf(" \n");
        if (vm.g_i >= 0)
            initprintf("current sprite: %d\n",vm.g_i);
        if (g_tw>=0 && g_tw<CON_END)
            initprintf("g_errorLineNum: %d, g_tw: %s\n",g_errorLineNum,keyw[g_tw]);
        else
            initprintf("g_errorLineNum: %d, g_tw: %d\n",g_errorLineNum,g_tw);
    }
}

void M32_PostScriptExec(void)
{
    if (vm.miscflags&VMFLAG_MISC_UPDATEHL)
    {
        update_highlight();
        vm.miscflags &= ~VMFLAG_MISC_UPDATEHL;
    }

    if (vm.miscflags&VMFLAG_MISC_UPDATEHLSECT)
    {
        update_highlightsector();
        if (!in3dmode())
            ovh_whiteoutgrab(1);
        vm.miscflags &= ~VMFLAG_MISC_UPDATEHLSECT;
    }
}

void VM_OnEvent(register int32_t iEventID, register int32_t iActor)
{
    if (iEventID < 0 || iEventID >= MAXEVENTS)
    {
        M32_PRINTERROR("Invalid event ID");
        return;
    }

    if (aEventOffsets[iEventID] < 0 || !aEventEnabled[iEventID])
    {
        //Bsprintf(g_szBuf,"No event found for %d",iEventID);
        //AddLog(g_szBuf);
        return;
    }

    {
        instype *const oinsptr=insptr;
        vmstate_t vm_backup;
        void *const olocalvars = aGameArrays[M32_LOCAL_ARRAY_ID].vals;
#ifdef M32_LOCALS_VARARRAY
        int32_t localvars[aEventNumLocals[iEventID]];
#else
        int32_t localvars[M32_LOCALS_FIXEDNUM];
#endif

        // Initialize 'state'-local variables to 0.
        if (aEventNumLocals[iEventID] > 0)
            Bmemset(localvars, 0, aEventNumLocals[iEventID]*sizeof(int32_t));

        Bmemcpy(&vm_backup, &vm, sizeof(vmstate_t));

        vm.g_i = iActor;    // current sprite ID
        if (vm.g_i >= 0)
            vm.g_sp = (tspritetype *)&sprite[vm.g_i];

        vm.g_st = 1+iEventID;

        vm.flags = 0;

        insptr = script + aEventOffsets[iEventID];

        aGameArrays[M32_LOCAL_ARRAY_ID].vals = localvars;
        VM_Execute(0);
        aGameArrays[M32_LOCAL_ARRAY_ID].vals = olocalvars;

        if (vm.flags&VMFLAG_ERROR)
        {
            aEventEnabled[iEventID] = 0;
            message("ERROR executing %s. Event disabled.", label+(iEventID*MAXLABELLEN));
        }

        M32_PostScriptExec();

        // restore old values...
        Bmemcpy(&vm, &vm_backup, sizeof(vmstate_t));
        insptr = oinsptr;

        //AddLog("End of Execution");
    }
}

static int32_t G_GetAngleDelta(int32_t a,int32_t na)
{
    a &= 2047;
    na &= 2047;

    if (klabs(a-na) < 1024)
    {
//        OSD_Printf("G_GetAngleDelta() returning %d\n",na-a);
        return (na-a);
    }

    if (na > 1024) na -= 2048;
    if (a > 1024) a -= 2048;

//    OSD_Printf("G_GetAngleDelta() returning %d\n",na-a);
    return (na-a);
}

static inline void __fastcall VM_DoConditional(register int32_t condition)
{
    if (condition)
    {
        // skip 'else' pointer.. and...
        insptr+=2;
        VM_Execute(1);
        return;
    }

    insptr++;
    insptr += *insptr;
    if (((*insptr)&0xFFF) == CON_ELSE)
    {
        // else...
        // skip 'else' and...
        insptr+=2;
        VM_Execute(1);
    }
}

static int X_DoSortDefault(const void *lv, const void *rv)
{
    return *(int32_t *)rv - *(int32_t *)lv;
}

static int X_DoSort(const void *lv, const void *rv)
{
    m32_sortvar1 = *(int32_t *)lv;
    m32_sortvar2 = *(int32_t *)rv;
    insptr = x_sortingstateptr;
    VM_Execute(0);
    return g_iReturnVar;
}

// in interactive execution, allow the current sprite index to be the aimed-at sprite (in 3d mode)
#define X_ERROR_INVALIDCI()                                                                                            \
    if ((vm.g_i < 0 || vm.g_i >= MAXSPRITES) &&                                                                        \
        (vm.g_st != 0 || searchstat != 3 || (vm.g_i = searchwall, vm.g_sp = (tspritetype *)&sprite[vm.g_i], 0)))       \
    {                                                                                                                  \
        M32_ERROR("Current sprite index invalid!");                                                                    \
        continue;                                                                                                      \
    }

#define X_ERROR_INVALIDSPRI(dasprite)                                   \
    if (dasprite < 0 || dasprite>=MAXSPRITES)                           \
    {                                                                   \
        M32_ERROR("Invalid sprite index %d!",  dasprite); \
        continue;                                                       \
    }

#define X_ERROR_INVALIDSECT(dasect)                                     \
    if (dasect < 0 || dasect>=numsectors)                               \
    {                                                                   \
        M32_ERROR("Invalid sector index %d!",  dasect); \
        continue;                                                       \
    }

#define X_ERROR_INVALIDSP()                                                                                            \
    if (!vm.g_sp && (vm.g_st != 0 || searchstat != 3 || (vm.g_sp = (tspritetype *)&sprite[searchwall], 0)))            \
    {                                                                                                                  \
        M32_ERROR("Current sprite invalid!");                                                                          \
        continue;                                                                                                      \
    }

#define X_ERROR_INVALIDQUOTE(q, array)                                  \
    if (q<0 || q>=MAXQUOTES)                                            \
    {                                                                   \
        M32_ERROR("Invalid quote number %d!",  q); \
        continue;                                                       \
    }                                                                   \
    else if (array[q] == NULL)                                          \
    {                                                                   \
        M32_ERROR("Null quote %d!",  q); \
        continue;                                                       \
    }                                                                   \

static char *GetMaybeInlineQuote(int32_t quotei)
{
    char *quotetext;
    if (quotei==-1)
    {
        quotetext = (char *)insptr;
        while (*insptr++) /* skip the string */;
    }
    else
    {
        quotei = Gv_GetVarX(quotei);
        do { X_ERROR_INVALIDQUOTE(quotei, ScriptQuotes) } while (0);
        if (vm.flags&VMFLAG_ERROR)
            return NULL;
        quotetext = ScriptQuotes[quotei];
    }

    return quotetext;
}

static int CheckArray(int aidx)
{
    if (!(aidx >= 0 && aidx < g_gameArrayCount))
        M32_ERROR("Invalid array %d!", aidx);

    return (vm.flags&VMFLAG_ERROR);
}

int32_t VM_Execute(int32_t once)
{
    register int32_t tw = *insptr;

    // jump directly into the loop, saving us from the checks during the first iteration
    goto skip_check;

    while (!once)
    {
        if (vm.flags)
            return 1;

        tw = *insptr;

skip_check:
        //      Bsprintf(g_szBuf,"Parsing: %d",*insptr);
        //      AddLog(g_szBuf);

        g_errorLineNum = tw>>12;
        g_tw = (tw &= 0xFFF);

        switch (tw)
        {
// *** basic commands
        case CON_NULLOP:
            insptr++;
            continue;

        case CON_STATE:
        {
            instype *const tempscrptr = insptr+2;
            const int32_t stateidx = *(insptr+1), o_g_st = vm.g_st, oret=vm.flags&VMFLAG_RETURN;
            void *const olocalvars = aGameArrays[M32_LOCAL_ARRAY_ID].vals;
#ifdef M32_LOCALS_VARARRAY
            int32_t localvars[statesinfo[stateidx].numlocals];
#else
            int32_t localvars[M32_LOCALS_FIXEDNUM];
#endif

            // needed since any read access before initialization would cause undefined behaviour
            if (statesinfo[stateidx].numlocals > 0)
                Bmemset(localvars, 0, statesinfo[stateidx].numlocals*sizeof(int32_t));

            insptr = script + statesinfo[stateidx].ofs;
            vm.g_st = 1+MAXEVENTS+stateidx;
            aGameArrays[M32_LOCAL_ARRAY_ID].vals = localvars;
            VM_Execute(0);
            aGameArrays[M32_LOCAL_ARRAY_ID].vals = olocalvars;
            vm.g_st = o_g_st;
            vm.flags &= ~VMFLAG_RETURN;
            vm.flags |= oret;
            insptr = tempscrptr;
        }
        continue;

        case CON_RETURN:
            vm.flags |= VMFLAG_RETURN;
            return 1;
        case CON_BREAK:
            vm.flags |= VMFLAG_BREAK;
            // XXX: may not be cleared subsequently?
        case CON_ENDS:
            return 1;

        case CON_ELSE:
            insptr++;
            insptr += *insptr;
            continue;

        case CON_ENDSWITCH:
            vm.flags &= ~VMFLAG_BREAK;
        case CON_ENDEVENT:
            insptr++;
            return 1;

        case CON_SWITCH:
            insptr++; // p-code
            {
                // command format:
                // variable ID to check
                // script offset to 'end'
                // count of case statements
                // script offset to default case (null if none)
                // For each case: value, ptr to code
                //AddLog("Processing Switch...");
                int32_t lValue=Gv_GetVarX(*insptr++), lEnd=*insptr++, lCases=*insptr++;
                instype *lpDefault=insptr++, *lpCases=insptr, *lCodeInsPtr;
                int32_t bMatched=0, lCheckCase;
                int32_t left,right;

                insptr += lCases*2;
                lCodeInsPtr = insptr;
                //Bsprintf(g_szBuf,"lEnd= %d *lpDefault=%d",lEnd,*lpDefault); AddLog(g_szBuf);
                //Bsprintf(g_szBuf,"Checking %d cases for %d",lCases, lValue); AddLog(g_szBuf);
                left = 0;
                right = lCases-1;
                while (!bMatched)
                {
                    //Bsprintf(g_szBuf,"Checking #%d Value= %d",lCheckCase, lpCases[lCheckCase*2]); AddLog(g_szBuf);
                    lCheckCase=(left+right)/2;
                    //                initprintf("(%2d..%2d..%2d) [%2d..%2d..%2d]==%2d\n",left,lCheckCase,right,lpCases[left*2],lpCases[lCheckCase*2],lpCases[right*2],lValue);
                    if (lpCases[lCheckCase*2] > lValue)
                        right = lCheckCase-1;
                    else if (lpCases[lCheckCase*2] < lValue)
                        left = lCheckCase+1;
                    else if (lpCases[lCheckCase*2] == lValue)
                    {
                        //AddLog("Found Case Match");
                        //Bsprintf(g_szBuf,"insptr=%d. lCheckCase=%d, offset=%d, &script[0]=%d", (int32_t)insptr,(int32_t)lCheckCase,lpCases[lCheckCase*2+1],(int32_t)&script[0]); AddLog(g_szBuf);
                        // fake a 2-d Array
                        insptr = lCodeInsPtr + lpCases[lCheckCase*2+1];
                        //Bsprintf(g_szBuf,"insptr=%d. ",     (int32_t)insptr); AddLog(g_szBuf);
                        VM_Execute(0);
                        //AddLog("Done Executing Case");
                        bMatched=1;
                    }

                    if (right-left < 0)
                        break;
                }

                if (!bMatched)
                {
                    if (*lpDefault >= 0)
                    {
                        //AddLog("No Matching Case: Using Default");
                        insptr = lCodeInsPtr + *lpDefault;
                        VM_Execute(0);
                    }
//                    else
//                    {
//                        //AddLog("No Matching Case: No Default to use");
//                    }
                }
                insptr = (instype *)(lCodeInsPtr + lEnd);
                vm.flags &= ~VMFLAG_BREAK;
                //Bsprintf(g_szBuf,"insptr=%d. ",     (int32_t)insptr); AddLog(g_szBuf);
                //AddLog("Done Processing Switch");
                continue;
            }

        case CON_GETCURRADDRESS:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, insptr-script);
            }
            continue;

        case CON_JUMP:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                if (j<0 || j>=(g_scriptPtr-script))
                {
                    M32_ERROR("script index out of bounds (%d)",  j);
                    continue;
                }
                insptr = (instype *)(j+script);
            }
            continue;

        case CON_RIGHTBRACE:
            insptr++;
            return 1;
        case CON_LEFTBRACE:
            insptr++;
            VM_Execute(0);
            continue;

// *** arrays
        case CON_SETARRAY:
            insptr++;
            {
                const int32_t j=*insptr++;
                const int32_t index = Gv_GetVarX(*insptr++);
                const int32_t value = Gv_GetVarX(*insptr++);

                CheckArray(j);

                if (aGameArrays[j].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Tried to set on read-only array `%s'", aGameArrays[j].szLabel);

                if (!(index >= 0 && index < aGameArrays[j].size))
                    M32_ERROR("Array index %d out of bounds", index);

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                // NOTE: Other array types not implemented, since they're read-only.
                ((int32_t *)aGameArrays[j].vals)[index] = value;
                continue;
            }

        case CON_GETARRAYSIZE:
            insptr++;
            {
                const int32_t j=*insptr++;

                if (CheckArray(j))
                    continue;

                Gv_SetVarX(*insptr++, Gv_GetArraySize(j));
            }
            continue;

        case CON_RESIZEARRAY:
            insptr++;
            {
                const int32_t j=*insptr++;
                const int32_t asize = Gv_GetVarX(*insptr++);

                CheckArray(j);

                if (aGameArrays[j].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Tried to resize read-only array `%s'", aGameArrays[j].szLabel);

                if (!(asize >= 1 && asize <= 65536))
                    M32_ERROR("Invalid array size %d (must be between 1 and 65536)", asize);

                if (vm.flags&VMFLAG_ERROR)
                    continue;

//                OSD_Printf(OSDTEXT_GREEN "CON_RESIZEARRAY: resizing array %s from %d to %d\n", aGameArrays[j].szLabel, aGameArrays[j].size, asize);
                aGameArrays[j].vals = Xrealloc(aGameArrays[j].vals, sizeof(int32_t) * asize);
                aGameArrays[j].size = asize;

                continue;
            }

        case CON_COPY:
            insptr++;
            {
                const int32_t si=*insptr++;
                int32_t sidx = Gv_GetVarX(*insptr++);
                const int32_t di=*insptr++;
                int32_t didx = Gv_GetVarX(*insptr++);
                int32_t numelts = Gv_GetVarX(*insptr++);

                CheckArray(si);
                CheckArray(di);

                if (aGameArrays[di].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Array %d is read-only!", di);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                const int32_t ssiz = Gv_GetArraySize(si);
                const int32_t dsiz = Gv_GetArraySize(di);

                if ((uint32_t)sidx >= (uint32_t)ssiz)
                    M32_ERROR("Invalid source index %d", sidx);
                if ((uint32_t)didx >= (uint32_t)dsiz)
                    M32_ERROR("Invalid destination index %d", didx);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                if (numelts > ssiz-sidx)
                    numelts = ssiz-sidx;
                if (numelts > dsiz-didx)
                    numelts = dsiz-didx;

                const gamearray_t *const sar = &aGameArrays[si];
                gamearray_t *const dar = &aGameArrays[di];

                switch (sar->dwFlags & GAMEARRAY_TYPE_MASK)
                {
                case 0:
                case GAMEARRAY_OFINT:
                    if (sar->dwFlags & GAMEARRAY_STRIDE2)
                    {
                        for (; numelts>0; numelts--, sidx += 2)
                            ((int32_t *)dar->vals)[didx++] = ((int32_t *)sar->vals)[sidx];
                    }
                    else
                    {
                        Bmemcpy((int32_t *)dar->vals + didx, (int32_t *)sar->vals + sidx,
                                numelts * sizeof(int32_t));
                    }
                    break;
                case GAMEARRAY_OFSHORT:
                    for (; numelts>0; numelts--)
                        ((int32_t *)dar->vals)[didx++] = ((int16_t *)sar->vals)[sidx++];
                    break;
                case GAMEARRAY_OFCHAR:
                    for (; numelts>0; numelts--)
                        ((int32_t *)dar->vals)[didx++] = ((uint8_t *)sar->vals)[sidx++];
                    break;
                }
                continue;
            }

// *** var & varvar ops
        case CON_RANDVAR:
            insptr++;
            Gv_SetVarX(*insptr, mulscale16(krand(), *(insptr+1)+1));
            insptr += 2;
            continue;

        case CON_DISPLAYRANDVAR:
            insptr++;
            Gv_SetVarX(*insptr, mulscale15(system_15bit_rand(), *(insptr+1)+1));
            insptr += 2;
            continue;

        case CON_SETVAR:
            insptr++;
            Gv_SetVarX(*insptr, *(insptr+1));
            insptr += 2;
            continue;

        case CON_SETVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_MULVAR:
            insptr++;
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) * *(insptr+1));
            insptr += 2;
            continue;

        case CON_DIVVAR:
            insptr++;
            if (*(insptr+1) == 0)
            {
                M32_ERROR("Divide by zero.");
                insptr += 2;
                continue;
            }
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) / *(insptr+1));
            insptr += 2;
            continue;

        case CON_MODVAR:
            insptr++;
            if (*(insptr+1) == 0)
            {
                M32_ERROR("Mod by zero.");
                insptr += 2;
                continue;
            }
            Gv_SetVarX(*insptr,Gv_GetVarX(*insptr)%*(insptr+1));
            insptr += 2;
            continue;

        case CON_ANDVAR:
            insptr++;
            Gv_SetVarX(*insptr,Gv_GetVarX(*insptr) & *(insptr+1));
            insptr += 2;
            continue;

        case CON_ORVAR:
            insptr++;
            Gv_SetVarX(*insptr,Gv_GetVarX(*insptr) | *(insptr+1));
            insptr += 2;
            continue;

        case CON_XORVAR:
            insptr++;
            Gv_SetVarX(*insptr,Gv_GetVarX(*insptr) ^ *(insptr+1));
            insptr += 2;
            continue;

        case CON_RANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j,mulscale16(krand(), Gv_GetVarX(*insptr++)+1));
            }
            continue;

        case CON_DISPLAYRANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j,mulscale15(system_15bit_rand(), Gv_GetVarX(*insptr++)+1));
            }
            continue;

        case CON_MULVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j)*Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_DIVVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                int32_t l2=Gv_GetVarX(*insptr++);

                if (l2==0)
                {
                    M32_ERROR("Divide by zero.");
                    continue;
                }
                Gv_SetVarX(j, Gv_GetVarX(j)/l2);
                continue;
            }

        case CON_MODVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                int32_t l2=Gv_GetVarX(*insptr++);

                if (l2==0)
                {
                    M32_ERROR("Mod by zero.");
                    continue;
                }

                Gv_SetVarX(j, Gv_GetVarX(j) % l2);
                continue;
            }

        case CON_ANDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) & Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_XORVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) ^ Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_ORVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) | Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_SUBVAR:
            insptr++;
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) - *(insptr+1));
            insptr += 2;
            continue;

        case CON_SUBVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) - Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_ADDVAR:
            insptr++;
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) + *(insptr+1));
            insptr += 2;
            continue;

        case CON_ADDVARVAR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) + Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_SHIFTVARL:
            insptr++;
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) << *(insptr+1));
            insptr += 2;
            continue;

        case CON_SHIFTVARVARL:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) << Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_SHIFTVARR:
            insptr++;
            Gv_SetVarX(*insptr, Gv_GetVarX(*insptr) >> *(insptr+1));
            insptr += 2;
            continue;

        case CON_SHIFTVARVARR:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, Gv_GetVarX(j) >> Gv_GetVarX(*insptr++));
            }
            continue;

        case CON_SIN:
            insptr++;
            Gv_SetVarX(*insptr, sintable[Gv_GetVarX(*(insptr+1))&2047]);
            insptr += 2;
            continue;

        case CON_COS:
            insptr++;
            Gv_SetVarX(*insptr, sintable[(Gv_GetVarX(*(insptr+1))+512)&2047]);
            insptr += 2;
            continue;

        case CON_DISPLAYRAND:
            insptr++;
            Gv_SetVarX(*insptr++, system_15bit_rand());
            continue;

// *** other math
        case CON_FTOI:
            insptr++;
            {
                int32_t bits=Gv_GetVarX(*insptr), scale=*(insptr+1);
                float fval = *((float *)&bits);
// rounding must absolutely be!
//OSD_Printf("ftoi: bits:%8x, scale=%d, fval=%f, (int32_t)(fval*scale)=%d\n", bits, scale, fval, (int32_t)(fval*scale));
                Gv_SetVarX(*insptr, (int32_t)Blrintf(fval * scale));
            }
            insptr += 2;
            continue;

        case CON_ITOF:
            insptr++;
            {
                int32_t scaled=Gv_GetVarX(*insptr), scale=*(insptr+1);
                float fval = (float)scaled/(float)scale;
                Gv_SetVarX(*insptr, *((int32_t *)&fval));
            }
            insptr += 2;
            continue;

        case CON_CLAMP:
            insptr++;
            {
                int32_t var=*insptr++, min=Gv_GetVarX(*insptr++), max=Gv_GetVarX(*insptr++);
                int32_t val=Gv_GetVarX(var);

                if (val<min) Gv_SetVarX(var, min);
                else if (val>max) Gv_SetVarX(var, max);
            }
            continue;

        case CON_INV:
            Gv_SetVarX(*(insptr+1), -Gv_GetVarX(*(insptr+1)));
            insptr += 2;
            continue;

        case CON_SQRT:
            insptr++;
            {
                // syntax sqrt <invar> <outvar>
                int32_t lInVarID=*insptr++, lOutVarID=*insptr++;

                Gv_SetVarX(lOutVarID, ksqrt((uint32_t)Gv_GetVarX(lInVarID)));
                continue;
            }

        case CON_LDIST:
        case CON_DIST:
            insptr++;
            {
                int32_t distvar = *insptr++, xvar = Gv_GetVarX(*insptr++), yvar = Gv_GetVarX(*insptr++);

                if (xvar < 0 || xvar >= MAXSPRITES || sprite[xvar].statnum==MAXSTATUS)
                {
                    M32_ERROR("invalid sprite %d", xvar);
                }
                if (yvar < 0 || yvar >= MAXSPRITES || sprite[yvar].statnum==MAXSTATUS)
                {
                    M32_ERROR("invalid sprite %d", yvar);
                }
                if (vm.flags&VMFLAG_ERROR) continue;

                if (tw==CON_DIST)
                    Gv_SetVarX(distvar, dist(&sprite[xvar],&sprite[yvar]));
                else
                    Gv_SetVarX(distvar, ldist(&sprite[xvar],&sprite[yvar]));
                continue;
            }

        case CON_GETANGLE:
            insptr++;
            {
                int32_t angvar = *insptr++;
                int32_t xvar = Gv_GetVarX(*insptr++);
                int32_t yvar = Gv_GetVarX(*insptr++);

                Gv_SetVarX(angvar, getangle(xvar,yvar));
                continue;
            }

        case CON_GETINCANGLE:
            insptr++;
            {
                int32_t angvar = *insptr++;
                int32_t xvar = Gv_GetVarX(*insptr++);
                int32_t yvar = Gv_GetVarX(*insptr++);

                Gv_SetVarX(angvar, G_GetAngleDelta(xvar,yvar));
                continue;
            }

        case CON_A2XY:
        case CON_AH2XYZ:
            insptr++;
            {
                int32_t ang=Gv_GetVarX(*insptr++), horiz=(tw==CON_A2XY)?100:Gv_GetVarX(*insptr++);
                int32_t xvar=*insptr++, yvar=*insptr++;

                int32_t x = sintable[(ang+512)&2047];
                int32_t y = sintable[ang&2047];

                if (tw==CON_AH2XYZ)
                {
                    int32_t zvar=*insptr++, z=0;

                    horiz -= 100;
                    if (horiz)
                    {
                        int32_t veclen = ksqrt(200*200 + horiz*horiz);
                        int32_t dacos = divscale14(200, veclen);

                        x = mulscale14(x, dacos);
                        y = mulscale14(y, dacos);
                        z = divscale14(-horiz, veclen);
                    }

                    Gv_SetVarX(zvar, z);
                }

                Gv_SetVarX(xvar, x);
                Gv_SetVarX(yvar, y);

                continue;
            }

        case CON_MULSCALE:
            insptr++;
            {
                int32_t var1 = *insptr++, var2 = Gv_GetVarX(*insptr++);
                int32_t var3 = Gv_GetVarX(*insptr++), var4 = Gv_GetVarX(*insptr++);

                Gv_SetVarX(var1, mulscale(var2, var3, var4));
                continue;
            }
        case CON_DIVSCALE:
            insptr++;
            {
                int32_t var1 = *insptr++, var2 = Gv_GetVarX(*insptr++);
                int32_t var3 = Gv_GetVarX(*insptr++), var4 = Gv_GetVarX(*insptr++);

                Gv_SetVarX(var1, divscale(var2, var3, var4));
                continue;
            }

// *** if & while
        case CON_IFVARVARAND:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j &= Gv_GetVarX(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVAROR:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j |= Gv_GetVarX(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARXOR:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j ^= Gv_GetVarX(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVAREITHER:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                int32_t l = Gv_GetVarX(*insptr++);
                insptr--;
                VM_DoConditional(j || l);
            }
            continue;

        case CON_IFVARVARBOTH:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                int32_t l = Gv_GetVarX(*insptr++);
                insptr--;
                VM_DoConditional(j && l);
            }
            continue;

        case CON_IFVARVARN:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j != Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARE:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j == Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARG:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j > Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARGE:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j >= Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARL:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j < Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARLE:
            insptr++;
            {
                int32_t j = Gv_GetVarX(*insptr++);
                j = (j <= Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARE:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j == *insptr);
            }
            continue;

        case CON_IFVARN:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j != *insptr);
            }
            continue;

        case CON_WHILEVARN:
        {
            instype *savedinsptr=insptr+2;
            int32_t j;
            do
            {
                insptr=savedinsptr;
                j = (Gv_GetVarX(*(insptr-1)) != *insptr);
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_WHILEVARL:
        {
            instype *savedinsptr=insptr+2;
            int32_t j;
            do
            {
                insptr=savedinsptr;
                j = (Gv_GetVarX(*(insptr-1)) < *insptr);
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_WHILEVARVARN:
        {
            int32_t j;
            instype *savedinsptr=insptr+2;
            do
            {
                insptr=savedinsptr;
                j = Gv_GetVarX(*(insptr-1));
                j = (j != Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_WHILEVARVARL:
        {
            int32_t j;
            instype *savedinsptr=insptr+2;
            do
            {
                insptr=savedinsptr;
                j = Gv_GetVarX(*(insptr-1));
                j = (j < Gv_GetVarX(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_COLLECTSECTORS:
            insptr++;
            {
                const int32_t aridx=*insptr++, startsectnum=Gv_GetVarX(*insptr++);
                const int32_t numsectsVar=*insptr++, state=*insptr++;

                if (CheckArray(aridx))
                    continue;

                gamearray_t *const gar = &aGameArrays[aridx];
                Bassert((gar->dwFlags & (GAMEARRAY_READONLY|GAMEARRAY_VARSIZE)) == 0);

                const int32_t o_g_st=vm.g_st, arsize = gar->size;
                instype *const end=insptr;
                int32_t sectcnt, numsects=0;

                // XXX: relies on -fno-strict-aliasing
                int16_t *const sectlist = (int16_t *)gar->vals;  // actually an int32_t array
                int32_t *const sectlist32 = (int32_t *)sectlist;

                int32_t j, startwall, endwall, ns;
                static uint8_t sectbitmap[MAXSECTORS>>3];

                X_ERROR_INVALIDSECT(startsectnum);
                if (arsize < numsectors)
                {
                    M32_ERROR("Array size must be at least numsectors (=%d) for collecting!",
                              numsectors);
                    continue;
                }

                // collect!
                bfirst_search_init(sectlist, sectbitmap, &numsects, MAXSECTORS, startsectnum);

                for (sectcnt=0; sectcnt<numsects; sectcnt++)
                    for (WALLS_OF_SECTOR(sectlist[sectcnt], j))
                        if ((ns=wall[j].nextsector) >= 0 && wall[j].nextsector<numsectors)
                        {
                            if (sectbitmap[ns>>3]&(1<<(ns&7)))
                                continue;
                            vm.g_st = 1+MAXEVENTS+state;
                            insptr = script + statesinfo[state].ofs;
                            g_iReturnVar = ns;
                            VM_Execute(0);
                            if (g_iReturnVar)
                                bfirst_search_try(sectlist, sectbitmap, &numsects, wall[j].nextsector);
                        }

                // short->int sector list
                for (j=numsects-1; j>=0; j--)
                    sectlist32[j] = sectlist[j];

                Gv_SetVarX(numsectsVar, numsects);
                g_iReturnVar = 0;

                // restore some VM state
                vm.g_st = o_g_st;
                insptr = end;
            }
            continue;

        case CON_SORT:
            insptr++;
            {
                const int32_t aridx=*insptr++, count=Gv_GetVarX(*insptr++), state=*insptr++;
                const int32_t o_g_st = vm.g_st;
                instype *const end = insptr;

                if (CheckArray(aridx))
                    continue;

                if (count <= 0)
                    continue;

                gamearray_t *const gar = &aGameArrays[aridx];
                Bassert((gar->dwFlags & (GAMEARRAY_READONLY|GAMEARRAY_VARSIZE)) == 0);

                if (count > gar->size)
                {
                    M32_ERROR("Count of elements to sort (%d) exceeds array size (%d)!",
                              count, gar->size);
                    continue;
                }

                if (state < 0)
                {
                    qsort(gar->vals, count, sizeof(int32_t), X_DoSortDefault);
                }
                else
                {
                    x_sortingstateptr = script + statesinfo[state].ofs;
                    vm.g_st = 1+MAXEVENTS+state;
                    qsort(gar->vals, count, sizeof(int32_t), X_DoSort);
                    vm.g_st = o_g_st;
                    insptr = end;
                }
            }
            continue;

        case CON_FOR:  // special-purpose iteration
            insptr++;
            {
                const int32_t var = *insptr++, how = *insptr++;
                const int32_t parm2 = how<=ITER_DRAWNSPRITES ? 0 : Gv_GetVarX(*insptr++);
                instype *const end = insptr + *insptr, *const beg = ++insptr;
                const int32_t vm_i_bak = vm.g_i;
                tspritetype *const vm_sp_bak = vm.g_sp;

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                switch (how)
                {
                case ITER_ALLSPRITES:
                    for (int jj=0; jj<MAXSPRITES && !vm.flags; jj++)
                    {
                        if (sprite[jj].statnum == MAXSTATUS)
                            continue;
                        Gv_SetVarX(var, jj);
                        vm.g_i = jj;
                        vm.g_sp = (tspritetype *)&sprite[jj];
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ALLSECTORS:
                    for (int jj=0; jj<numsectors && !vm.flags; jj++)
                    {
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ALLWALLS:
                    for (int jj=0; jj<numwalls && !vm.flags; jj++)
                    {
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_ACTIVELIGHTS:
#ifdef POLYMER
                    for (int jj=0; jj<PR_MAXLIGHTS; jj++)
                    {
                        if (!prlights[jj].flags.active)
                            continue;

                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
#else
                    M32_ERROR("Polymer not compiled in, iteration over lights forbidden.");
#endif
                    break;

                case ITER_SELSPRITES:
                    for (int ii=0; ii<highlightcnt && !vm.flags; ii++)
                    {
                        int jj = highlight[ii];
                        if (jj&0xc000)
                        {
                            jj &= (MAXSPRITES-1);
                            Gv_SetVarX(var, jj);
                            vm.g_i = jj;
                            vm.g_sp = (tspritetype *)&sprite[jj];
                            insptr = beg;
                            VM_Execute(1);
                        }
                    }
                    break;
                case ITER_SELSECTORS:
                    for (int ii=0; ii<highlightsectorcnt && !vm.flags; ii++)
                    {
                        int jj=highlightsector[ii];
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_SELWALLS:
                    for (int ii=0; ii<highlightcnt && !vm.flags; ii++)
                    {
                        int jj=highlight[ii];
                        if (jj&0xc000)
                            continue;
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_DRAWNSPRITES:
                {
                    tspritetype lastSpriteBackup;
                    tspritetype *const lastSpritePtr = (tspritetype *)&sprite[MAXSPRITES-1];

                    // Back up sprite MAXSPRITES-1.
                    Bmemcpy(&lastSpriteBackup, lastSpritePtr, sizeof(tspritetype));

                    for (int ii=0; ii<spritesortcnt && !vm.flags; ii++)
                    {
                        vm.g_sp = lastSpritePtr;
                        Bmemcpy(lastSpritePtr, &tsprite[ii], sizeof(tspritetype));

                        Gv_SetVarX(var, ii);
                        insptr = beg;
                        VM_Execute(1);

                        // Copy over potentially altered tsprite.
                        Bmemcpy(&tsprite[ii], lastSpritePtr, sizeof(tspritetype));
                    }

                    // Restore sprite MAXSPRITES-1.
                    Bmemcpy(lastSpritePtr, &lastSpriteBackup, sizeof(tspritetype));
                    break;
                }
                case ITER_SPRITESOFSECTOR:
                    if (parm2 < 0 || parm2 >= MAXSECTORS)
                        goto badindex;
                    for (int jj=headspritesect[parm2]; jj>=0 && !vm.flags; jj=nextspritesect[jj])
                    {
                        Gv_SetVarX(var, jj);
                        vm.g_i = jj;
                        vm.g_sp = (tspritetype *)&sprite[jj];
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_WALLSOFSECTOR:
                    if (parm2 < 0 || parm2 >= MAXSECTORS)
                        goto badindex;
                    for (int jj=sector[parm2].wallptr, endwall=jj+sector[parm2].wallnum-1;
                            jj<=endwall && !vm.flags; jj++)
                    {
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                case ITER_LOOPOFWALL:
                    if (parm2 < 0 || parm2 >= numwalls)
                        goto badindex;
                    {
                        int jj = parm2;
                        do
                        {
                            Gv_SetVarX(var, jj);
                            insptr = beg;
                            VM_Execute(1);
                            jj = wall[jj].point2;
                        }
                        while (jj != parm2 && !vm.flags);
                    }
                    break;
                case ITER_RANGE:
                    for (int jj=0; jj<parm2 && !vm.flags; jj++)
                    {
                        Gv_SetVarX(var, jj);
                        insptr = beg;
                        VM_Execute(1);
                    }
                    break;
                default:
                    M32_ERROR("Unknown iteration type %d!", how);
                    continue;
badindex:
                    OSD_Printf(OSD_ERROR "Line %d, %s %s: index %d out of range!\n",g_errorLineNum,keyw[g_tw],
                               iter_tokens[how].token, parm2);
                    vm.flags |= VMFLAG_ERROR;
                    continue;
                }
                vm.g_i = vm_i_bak;
                vm.g_sp = vm_sp_bak;
                vm.flags &= ~VMFLAG_BREAK;
                insptr = end;
            }
            continue;

        case CON_IFVARAND:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j & *insptr);
            }
            continue;

        case CON_IFVAROR:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j | *insptr);
            }
            continue;

        case CON_IFVARXOR:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j ^ *insptr);
            }
            continue;

        case CON_IFVAREITHER:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j || *insptr);
            }
            continue;

        case CON_IFVARBOTH:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j && *insptr);
            }
            continue;

        case CON_IFVARG:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j > *insptr);
            }
            continue;

        case CON_IFVARGE:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j >= *insptr);
            }
            continue;

        case CON_IFVARL:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j < *insptr);
            }
            continue;

        case CON_IFVARLE:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);
                VM_DoConditional(j <= *insptr);
            }
            continue;

        case CON_IFRND:
            VM_DoConditional(rnd(Gv_GetVarX(*(++insptr))));
            continue;

        case CON_IFHITKEY:
        case CON_IFHOLDKEY:
        case CON_RESETKEY:
        case CON_SETKEY:
            insptr++;
            {
                int32_t key=Gv_GetVarX(*insptr);
                if (key<0 || key >= (int32_t)ARRAY_SIZE(keystatus))
                {
                    M32_ERROR("Invalid key %d!", key);
                    continue;
                }

                if (tw == CON_IFHITKEY || tw == CON_IFHOLDKEY)
                    VM_DoConditional(keystatus[key]);
                else
                    insptr++;

                if (tw != CON_IFHOLDKEY)
                {
                    if (!(key==0 || key==KEYSC_ESC || key==KEYSC_TILDE || key==KEYSC_gENTER ||
                            key==KEYSC_LALT || key==KEYSC_RALT || key==KEYSC_LCTRL || key==KEYSC_RCTRL ||
                            key==KEYSC_LSHIFT || key==KEYSC_RSHIFT))
                        keystatus[key] = (tw==CON_SETKEY);
                }
            }
            continue;

        case CON_IFEITHERALT:
            VM_DoConditional(keystatus[KEYSC_LALT]||keystatus[KEYSC_RALT]);
            continue;

        case CON_IFEITHERCTRL:
            VM_DoConditional(keystatus[KEYSC_LCTRL]||keystatus[KEYSC_RCTRL]);
            continue;

        case CON_IFEITHERSHIFT:
            VM_DoConditional(keystatus[KEYSC_LSHIFT]||keystatus[KEYSC_RSHIFT]);
            continue;

// vvv CURSPR
        case CON_IFSPRITEPAL:
            insptr++;
            X_ERROR_INVALIDSP();
            VM_DoConditional(vm.g_sp->pal == Gv_GetVarX(*insptr));
            continue;

        case CON_IFHIGHLIGHTED:
            insptr++;
            {
                int32_t id=*insptr++, index=Gv_GetVarX(*insptr);

                if (index<0 || (id==M32_SPRITE_VAR_ID && index>=MAXSPRITES) || (id==M32_WALL_VAR_ID && index>=numwalls))
                {
                    M32_ERROR("%s index %d out of range!", id==M32_SPRITE_VAR_ID?"Sprite":"Wall", index);
                    continue;
                }

                if (id==M32_SPRITE_VAR_ID)
                    VM_DoConditional(show2dsprite[index>>3]&(1<<(index&7)));
                else
                    VM_DoConditional(show2dwall[index>>3]&(1<<(index&7)));
            }
            continue;

        case CON_IFANGDIFFL:
            insptr++;
            {
                int32_t j;
                X_ERROR_INVALIDSP();
                j = klabs(G_GetAngleDelta(ang, vm.g_sp->ang));
                VM_DoConditional(j <= Gv_GetVarX(*insptr));
            }
            continue;

        case CON_IFAWAYFROMWALL:
        {
            int16_t s1;
            int32_t j = 0;

            X_ERROR_INVALIDSP();
            s1 = vm.g_sp->sectnum;
            updatesector(vm.g_sp->x+108,vm.g_sp->y+108,&s1);
            if (s1 == vm.g_sp->sectnum)
            {
                updatesector(vm.g_sp->x-108,vm.g_sp->y-108,&s1);
                if (s1 == vm.g_sp->sectnum)
                {
                    updatesector(vm.g_sp->x+108,vm.g_sp->y-108,&s1);
                    if (s1 == vm.g_sp->sectnum)
                    {
                        updatesector(vm.g_sp->x-108,vm.g_sp->y+108,&s1);
                        if (s1 == vm.g_sp->sectnum)
                            j = 1;
                    }
                }
            }
            VM_DoConditional(j);
        }
        continue;

        case CON_IFCANSEE:
        {
            int32_t j;

            X_ERROR_INVALIDSP();
            j = cansee(vm.g_sp->x,vm.g_sp->y,vm.g_sp->z/*-((krand()&41)<<8)*/,vm.g_sp->sectnum,
                       pos.x, pos.y, pos.z /*-((krand()&41)<<8)*/, cursectnum);
            VM_DoConditional(j);
        }
        continue;

        case CON_IFONWATER:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.g_sp->sectnum].lotag == 1 && klabs(vm.g_sp->z-sector[vm.g_sp->sectnum].floorz) < (32<<8));
            continue;

        case CON_IFINWATER:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.g_sp->sectnum].lotag == 2);
            continue;

        case CON_IFACTOR:
            insptr++;
            X_ERROR_INVALIDSP();
            VM_DoConditional(vm.g_sp->picnum == Gv_GetVarX(*insptr));
            continue;

        case CON_IFINSIDE:
            insptr++;
            {
                int32_t x=Gv_GetVarX(*insptr++), y=Gv_GetVarX(*insptr++), sectnum=Gv_GetVarX(*insptr++), res;

                res = inside(x, y, sectnum);
                if (res == -1)
                {
                    M32_ERROR("Sector index %d out of range!", sectnum);
                    continue;
                }
                insptr--;
                VM_DoConditional(res);
            }
            continue;

        case CON_IFOUTSIDE:
            X_ERROR_INVALIDSP();
            VM_DoConditional(sector[vm.g_sp->sectnum].ceilingstat&1);
            continue;

        case CON_IFPDISTL:
            insptr++;
            {
                X_ERROR_INVALIDSP();
                VM_DoConditional(dist((spritetype *)&pos, vm.g_sp) < Gv_GetVarX(*insptr));
            }
            continue;

        case CON_IFPDISTG:
            insptr++;
            {
                X_ERROR_INVALIDSP();
                VM_DoConditional(dist((spritetype *)&pos, vm.g_sp) > Gv_GetVarX(*insptr));
            }
            continue;
// ^^^

// *** BUILD functions
        case CON_INSERTSPRITE:
            insptr++;
            {
                int32_t dasectnum = Gv_GetVarX(*insptr++), ret;

                X_ERROR_INVALIDSECT(dasectnum);
                if (Numsprites >= MAXSPRITES)
                {
                    M32_ERROR("Maximum number of sprites reached.");
                    continue;
                }

                ret = insertsprite(dasectnum, 0);
                vm.g_i = ret;
                vm.g_sp = (tspritetype *)&sprite[ret];
            }
            continue;

        case CON_DUPSPRITE:
        case CON_TDUPSPRITE:
            insptr++;
            {
                int32_t ospritenum = Gv_GetVarX(*insptr++), nspritenum;

                if (ospritenum<0 || ospritenum>=MAXSPRITES || sprite[ospritenum].statnum==MAXSTATUS)
                {
                    M32_ERROR("Tried to duplicate nonexistent sprite %d", ospritenum);
                }
                if ((tw==CON_DUPSPRITE && Numsprites >= MAXSPRITES) ||
                        (tw==CON_DUPSPRITE && spritesortcnt >= MAXSPRITESONSCREEN))
                {
                    M32_ERROR("Maximum number of sprites reached.");
                }

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                if (tw==CON_DUPSPRITE)
                {
                    nspritenum = insertsprite(sprite[ospritenum].sectnum, sprite[ospritenum].statnum);

                    if (nspritenum < 0)
                    {
                        M32_ERROR("Internal error.");
                        continue;
                    }

                    Bmemcpy(&sprite[nspritenum], &sprite[ospritenum], sizeof(spritetype));
                    vm.g_i = nspritenum;
                    vm.g_sp = (tspritetype *)&sprite[nspritenum];
                }
                else
                {
                    Bmemcpy(&tsprite[spritesortcnt], &sprite[ospritenum], sizeof(spritetype));
                    tsprite[spritesortcnt].owner = ospritenum;
                    vm.g_i = -1;
                    vm.g_sp = &tsprite[spritesortcnt];
                    spritesortcnt++;
                }
            }
            continue;

        case CON_DELETESPRITE:
            insptr++;
            {
                int32_t daspritenum = Gv_GetVarX(*insptr++), ret;

                X_ERROR_INVALIDSPRI(daspritenum);
                ret = deletesprite(daspritenum);
                g_iReturnVar = ret;
            }
            continue;

        case CON_GETSPRITELINKTYPE:
            insptr++;
            {
                int32_t spritenum=Gv_GetVarX(*insptr++), resvar = *insptr++;

                X_ERROR_INVALIDSPRI(spritenum);
                Gv_SetVarX(resvar, taglab_linktags(1, spritenum));
            }
            continue;

        case CON_LASTWALL:
            insptr++;
            {
                int32_t dapoint = Gv_GetVarX(*insptr++), resvar=*insptr++;

                if (dapoint<0 || dapoint>=numwalls)
                {
                    M32_ERROR("Invalid wall %d", dapoint);
                    continue;
                }

                Gv_SetVarX(resvar, lastwall(dapoint));
            }
            continue;

        case CON_GETZRANGE:
            insptr++;
            {
                vec3_t vect;

                vect.x = Gv_GetVarX(*insptr++);
                vect.y = Gv_GetVarX(*insptr++);
                vect.z = Gv_GetVarX(*insptr++);

                {
                    int32_t sectnum=Gv_GetVarX(*insptr++);
                    int32_t ceilzvar=*insptr++, ceilhitvar=*insptr++, florzvar=*insptr++, florhitvar=*insptr++;
                    int32_t walldist=Gv_GetVarX(*insptr++), clipmask=Gv_GetVarX(*insptr++);
                    int32_t ceilz, ceilhit, florz, florhit;

                    X_ERROR_INVALIDSECT(sectnum);
                    getzrange(&vect, sectnum, &ceilz, &ceilhit, &florz, &florhit, walldist, clipmask);
                    Gv_SetVarX(ceilzvar, ceilz);
                    Gv_SetVarX(ceilhitvar, ceilhit);
                    Gv_SetVarX(florzvar, florz);
                    Gv_SetVarX(florhitvar, florhit);
                }
                continue;
            }

        case CON_CALCHYPOTENUSE:
            insptr++;
            {
                int32_t retvar=*insptr++;
                int64_t dax=Gv_GetVarX(*insptr++), day=Gv_GetVarX(*insptr++);
                int64_t hypsq = dax*dax + day*day;

                if (hypsq > (int64_t)INT32_MAX)
                    Gv_SetVarX(retvar, (int32_t)sqrt((double)hypsq));
                else
                    Gv_SetVarX(retvar, ksqrt((uint32_t)hypsq));

                continue;
            }

        case CON_LINEINTERSECT:
        case CON_RAYINTERSECT:
            insptr++;
            {
                int32_t x1=Gv_GetVarX(*insptr++), y1=Gv_GetVarX(*insptr++), z1=Gv_GetVarX(*insptr++);
                int32_t x2=Gv_GetVarX(*insptr++), y2=Gv_GetVarX(*insptr++), z2=Gv_GetVarX(*insptr++);
                int32_t x3=Gv_GetVarX(*insptr++), y3=Gv_GetVarX(*insptr++), x4=Gv_GetVarX(*insptr++), y4=Gv_GetVarX(*insptr++);
                int32_t intxvar=*insptr++, intyvar=*insptr++, intzvar=*insptr++, retvar=*insptr++;
                int32_t intx, inty, intz, ret;

                if (tw==CON_LINEINTERSECT)
                    ret = lintersect(x1, y1, z1, x2, y2, z2, x3, y3, x4, y4, &intx, &inty, &intz);
                else
                    ret = rayintersect(x1, y1, z1, x2, y2, z2, x3, y3, x4, y4, &intx, &inty, &intz);

                Gv_SetVarX(retvar, ret);
                if (ret)
                {
                    Gv_SetVarX(intxvar, intx);
                    Gv_SetVarX(intyvar, inty);
                    Gv_SetVarX(intzvar, intz);
                }

                continue;
            }

        case CON_CLIPMOVE:
            insptr++;
            {
                vec3_t vect;
                int32_t retvar=*insptr++, xvar=*insptr++, yvar=*insptr++, z=Gv_GetVarX(*insptr++), sectnumvar=*insptr++;
                int32_t xvect=Gv_GetVarX(*insptr++), yvect=Gv_GetVarX(*insptr++);
                int32_t walldist=Gv_GetVarX(*insptr++), floordist=Gv_GetVarX(*insptr++), ceildist=Gv_GetVarX(*insptr++);
                int32_t clipmask=Gv_GetVarX(*insptr++);
                int16_t sectnum;

                vect.x = Gv_GetVarX(xvar);
                vect.y = Gv_GetVarX(yvar);
                vect.z = z;
                sectnum = Gv_GetVarX(sectnumvar);

                X_ERROR_INVALIDSECT(sectnum);

                Gv_SetVarX(retvar, clipmove(&vect, &sectnum, xvect, yvect, walldist, floordist, ceildist, clipmask));
                Gv_SetVarX(sectnumvar, sectnum);
                Gv_SetVarX(xvar, vect.x);
                Gv_SetVarX(yvar, vect.y);

                continue;
            }

        case CON_HITSCAN:
            insptr++;
            {
                vec3_t vect;
                hitdata_t hit;

                vect.x = Gv_GetVarX(*insptr++);
                vect.y = Gv_GetVarX(*insptr++);
                vect.z = Gv_GetVarX(*insptr++);

                {
                    int32_t sectnum=Gv_GetVarX(*insptr++);
                    int32_t vx=Gv_GetVarX(*insptr++), vy=Gv_GetVarX(*insptr++), vz=Gv_GetVarX(*insptr++);
                    int32_t hitsectvar=*insptr++, hitwallvar=*insptr++, hitspritevar=*insptr++;
                    int32_t hitxvar=*insptr++, hityvar=*insptr++, hitzvar=*insptr++, cliptype=Gv_GetVarX(*insptr++);

                    X_ERROR_INVALIDSECT(sectnum);
                    hitscan((const vec3_t *)&vect, sectnum, vx, vy, vz, &hit, cliptype);
                    Gv_SetVarX(hitsectvar, hit.sect);
                    Gv_SetVarX(hitwallvar, hit.wall);
                    Gv_SetVarX(hitspritevar, hit.sprite);
                    Gv_SetVarX(hitxvar, hit.pos.x);
                    Gv_SetVarX(hityvar, hit.pos.y);
                    Gv_SetVarX(hitzvar, hit.pos.z);
                }
                continue;
            }

        case CON_CANSEE:
            insptr++;
            {
                int32_t x1=Gv_GetVarX(*insptr++), y1=Gv_GetVarX(*insptr++), z1=Gv_GetVarX(*insptr++);
                int32_t sect1=Gv_GetVarX(*insptr++);
                int32_t x2=Gv_GetVarX(*insptr++), y2=Gv_GetVarX(*insptr++), z2=Gv_GetVarX(*insptr++);
                int32_t sect2=Gv_GetVarX(*insptr++), rvar=*insptr++;

                X_ERROR_INVALIDSECT(sect1);
                X_ERROR_INVALIDSECT(sect2);

                Gv_SetVarX(rvar, cansee(x1,y1,z1,sect1,x2,y2,z2,sect2));
                continue;
            }

        case CON_ROTATEPOINT:
            insptr++;
            {
                vec2_t pivot = { Gv_GetVarX(*insptr), Gv_GetVarX(*(insptr+1)) };
                vec2_t p = { Gv_GetVarX(*(insptr+2)), Gv_GetVarX(*(insptr+3)) };
                insptr += 4;
                int32_t daang=Gv_GetVarX(*insptr++);
                int32_t x2var=*insptr++, y2var=*insptr++;
                vec2_t p2;

                rotatepoint(pivot,p,daang,&p2);
                Gv_SetVarX(x2var, p2.x);
                Gv_SetVarX(y2var, p2.y);
                continue;
            }

        case CON_NEARTAG:
            insptr++;
            {
                // neartag(int32_t x, int32_t y, int32_t z, short sectnum, short ang,  //Starting position & angle
                //         short *neartagsector,    //Returns near sector if sector[].tag != 0
                //         short *neartagwall,      //Returns near wall if wall[].tag != 0
                //         short *neartagsprite,    //Returns near sprite if sprite[].tag != 0
                //         int32_t *neartaghitdist, //Returns actual distance to object (scale: 1024=largest grid size)
                //         int32_t neartagrange,    //Choose maximum distance to scan (scale: 1024=largest grid size)
                //         char tagsearch)          //1-lotag only, 2-hitag only, 3-lotag&hitag

                int32_t x=Gv_GetVarX(*insptr++), y=Gv_GetVarX(*insptr++), z=Gv_GetVarX(*insptr++);
                int32_t sectnum=Gv_GetVarX(*insptr++), ang=Gv_GetVarX(*insptr++);
                int32_t neartagsectorvar=*insptr++, neartagwallvar=*insptr++, neartagspritevar=*insptr++, neartaghitdistvar=*insptr++;
                int32_t neartagrange=Gv_GetVarX(*insptr++), tagsearch=Gv_GetVarX(*insptr++);

                int16_t neartagsector, neartagwall, neartagsprite;
                int32_t neartaghitdist;

                X_ERROR_INVALIDSECT(sectnum);
                neartag(x, y, z, sectnum, ang, &neartagsector, &neartagwall, &neartagsprite,
                        &neartaghitdist, neartagrange, tagsearch, NULL);

                Gv_SetVarX(neartagsectorvar, neartagsector);
                Gv_SetVarX(neartagwallvar, neartagwall);
                Gv_SetVarX(neartagspritevar, neartagsprite);
                Gv_SetVarX(neartaghitdistvar, neartaghitdist);
                continue;
            }

        case CON_BSETSPRITE:  // was CON_SETSPRITE
            insptr++;
            {
                int32_t spritenum = Gv_GetVarX(*insptr++);
                vec3_t davector;

                davector.x = Gv_GetVarX(*insptr++);
                davector.y = Gv_GetVarX(*insptr++);
                davector.z = Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(spritenum);
                setsprite(spritenum, &davector);
                continue;
            }

        case CON_GETFLORZOFSLOPE:
        case CON_GETCEILZOFSLOPE:
            insptr++;
            {
                int32_t sectnum = Gv_GetVarX(*insptr++), x = Gv_GetVarX(*insptr++), y = Gv_GetVarX(*insptr++);
                int32_t var=*insptr++;

                X_ERROR_INVALIDSECT(sectnum);
                if (tw == CON_GETFLORZOFSLOPE)
                    Gv_SetVarX(var, getflorzofslope(sectnum,x,y));
                else
                    Gv_SetVarX(var, getceilzofslope(sectnum,x,y));
                continue;
            }

        case CON_ALIGNFLORSLOPE:
        case CON_ALIGNCEILSLOPE:
            insptr++;
            {
                int32_t sectnum = Gv_GetVarX(*insptr++), x = Gv_GetVarX(*insptr++), y = Gv_GetVarX(*insptr++);
                int32_t z=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSECT(sectnum);
                if (tw == CON_ALIGNFLORSLOPE)
                    alignflorslope(sectnum, x,y,z);
                else
                    alignceilslope(sectnum, x,y,z);
                continue;
            }

// CURSPR
        case CON_SETFIRSTWALL:
            insptr++;
            {
                int32_t sect=Gv_GetVarX(*insptr++), wal=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSECT(sect);
                setfirstwall(sect, wal);
            }
            continue;

        case CON_UPDATECURSECTNUM:
            insptr++;
            updatesectorz(pos.x, pos.y, pos.z, &cursectnum);
            continue;

        case CON_UPDATESECTOR:
        case CON_UPDATESECTORZ:
            insptr++;
            {
                int32_t x=Gv_GetVarX(*insptr++), y=Gv_GetVarX(*insptr++);
                int32_t z=(tw==CON_UPDATESECTORZ)?Gv_GetVarX(*insptr++):0;
                int32_t var=*insptr++;
                int16_t w;

                X_ERROR_INVALIDCI();
                w=sprite[vm.g_i].sectnum;

                if (tw==CON_UPDATESECTOR) updatesector(x,y,&w);
                else updatesectorz(x,y,z,&w);

                Gv_SetVarX(var, w);
                continue;
            }

        case CON_HEADSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);
                if (j < 0 || j > MAXSTATUS)
                {
                    M32_ERROR("invalid status list %d", j);
                    continue;
                }
                Gv_SetVarX(i,headspritestat[j]);
                continue;
            }

        case CON_PREVSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                Gv_SetVarX(i,prevspritestat[j]);
                continue;
            }

        case CON_NEXTSPRITESTAT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                Gv_SetVarX(i,nextspritestat[j]);
                continue;
            }

        case CON_HEADSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSECT(j);
                Gv_SetVarX(i,headspritesect[j]);
                continue;
            }

        case CON_PREVSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                Gv_SetVarX(i,prevspritesect[j]);
                continue;
            }

        case CON_NEXTSPRITESECT:
            insptr++;
            {
                int32_t i=*insptr++;
                int32_t j=Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(j);
                Gv_SetVarX(i,nextspritesect[j]);
                continue;
            }

        case CON_CANSEESPR:
            insptr++;
            {
                int32_t lVar1 = Gv_GetVarX(*insptr++), lVar2 = Gv_GetVarX(*insptr++), res;

                if (lVar1<0 || lVar1>=MAXSPRITES || sprite[lVar1].statnum==MAXSTATUS)
                {
                    M32_ERROR("Invalid sprite %d", lVar1);
                }
                if (lVar2<0 || lVar2>=MAXSPRITES || sprite[lVar2].statnum==MAXSTATUS)
                {
                    M32_ERROR("Invalid sprite %d", lVar2);
                }

                if (vm.flags&VMFLAG_ERROR) res=0;
                else res=cansee(sprite[lVar1].x,sprite[lVar1].y,sprite[lVar1].z,sprite[lVar1].sectnum,
                                    sprite[lVar2].x,sprite[lVar2].y,sprite[lVar2].z,sprite[lVar2].sectnum);

                Gv_SetVarX(*insptr++, res);
                continue;
            }

        case CON_CHANGESPRITESTAT:
        case CON_CHANGESPRITESECT:
            insptr++;
            {
                int32_t i = Gv_GetVarX(*insptr++);
                int32_t j = Gv_GetVarX(*insptr++);

                X_ERROR_INVALIDSPRI(i);
                if (j<0 || j >= (tw==CON_CHANGESPRITESTAT?MAXSTATUS:numsectors))
                {
                    M32_ERROR("Invalid %s: %d", tw==CON_CHANGESPRITESTAT?"statnum":"sector", j);
                    continue;
                }

                if (tw == CON_CHANGESPRITESTAT)
                {
                    if (sprite[i].statnum == j) continue;
                    changespritestat(i,j);
                }
                else
                {
                    if (sprite[i].sectnum == j) continue;
                    changespritesect(i,j);
                }
                continue;
            }

        case CON_DRAGPOINT:
            insptr++;
            {
                int32_t wallnum = Gv_GetVarX(*insptr++), newx = Gv_GetVarX(*insptr++), newy = Gv_GetVarX(*insptr++);

                if (wallnum<0 || wallnum>=numwalls)
                {
                    M32_ERROR("Invalid wall %d", wallnum);
                    continue;
                }
                dragpoint(wallnum,newx,newy,0);
                continue;
            }

        case CON_SECTOROFWALL:
            insptr++;
            {
                int32_t j = *insptr++;
                Gv_SetVarX(j, sectorofwall(Gv_GetVarX(*insptr++)));
            }
            continue;

        case CON_FIXREPEATS:
            insptr++;
            fixrepeats(Gv_GetVarX(*insptr++));
            continue;

        case CON_GETCLOSESTCOL:
            insptr++;
            {
                int32_t r = Gv_GetVarX(*insptr++), g = Gv_GetVarX(*insptr++), b = Gv_GetVarX(*insptr++);
                Gv_SetVarX(*insptr++, getclosestcol(r, g, b));
                continue;
            }

// *** stuff
        case CON_UPDATEHIGHLIGHT:
            insptr++;
            update_highlight();
            continue;

        case CON_UPDATEHIGHLIGHTSECTOR:
            insptr++;
            update_highlightsector();
            continue;

        case CON_SETHIGHLIGHT:
            insptr++;
            {
                int32_t what=Gv_GetVarX(*insptr++), index=Gv_GetVarX(*insptr++), doset = Gv_GetVarX(*insptr++);

                if (highlightsectorcnt >= 0)
                {
                    M32_ERROR("sector highlight active or pending, cannot highlight sprites/walls");
                    continue;
                }

                if (what&16384)
                {
                    index &= ~16384;
                    if (index < 0 || index>=MAXSPRITES || sprite[index].statnum==MAXSTATUS)
                    {
                        M32_ERROR("Invalid sprite index %d", index);
                        continue;
                    }

                    if (doset)
                        show2dsprite[index>>3] |= (1<<(index&7));
                    else
                        show2dsprite[index>>3] &= ~(1<<(index&7));
                }
                else
                {
                    if (index < 0 || index>=numwalls)
                    {
                        M32_ERROR("Invalid wall index %d", index);
                        continue;
                    }

                    if (doset)
                        show2dwall[index>>3] |= (1<<(index&7));
                    else
                        show2dwall[index>>3] &= ~(1<<(index&7));
                }

                vm.miscflags |= VMFLAG_MISC_UPDATEHL;

                continue;
            }

        case CON_SETHIGHLIGHTSECTOR:
            insptr++;
            {
                int32_t index=Gv_GetVarX(*insptr++), doset = Gv_GetVarX(*insptr++);

                if (highlightcnt >= 0)
                {
                    M32_ERROR("sprite/wall highlight active or pending, cannot highlight sectors");
                    continue;
                }

                X_ERROR_INVALIDSECT(index);

                if (doset)
                    hlsectorbitmap[index>>3] |= (1<<(index&7));
                else
                    hlsectorbitmap[index>>3] &= ~(1<<(index&7));

                vm.miscflags |= VMFLAG_MISC_UPDATEHLSECT;

                continue;
            }

        case CON_GETTIMEDATE:
            insptr++;
            {
                int32_t v1=*insptr++,v2=*insptr++,v3=*insptr++,v4=*insptr++,v5=*insptr++,v6=*insptr++,v7=*insptr++,v8=*insptr++;
                time_t rawtime;
                struct tm *ti;

                time(&rawtime);
                ti = localtime(&rawtime);
                // initprintf("Time&date: %s\n",asctime (ti));

                Gv_SetVarX(v1, ti->tm_sec);
                Gv_SetVarX(v2, ti->tm_min);
                Gv_SetVarX(v3, ti->tm_hour);
                Gv_SetVarX(v4, ti->tm_mday);
                Gv_SetVarX(v5, ti->tm_mon);
                Gv_SetVarX(v6, ti->tm_year+1900);
                Gv_SetVarX(v7, ti->tm_wday);
                Gv_SetVarX(v8, ti->tm_yday);
                continue;
            }

        case CON_ADDLOG:
        {
            insptr++;

            OSD_Printf("L=%d\n", g_errorLineNum);
            continue;
        }

        case CON_ADDLOGVAR:
            insptr++;
            {
                char buf[80] = "", buf2[80] = "";
                int32_t code = (int32_t)*insptr, val = Gv_GetVarX(code);
                int32_t negate=code&M32_FLAG_NEGATE;

                if (code & (0xFFFFFFFF-(MAXGAMEVARS-1)))
                {
                    if ((code&M32_VARTYPE_MASK)==M32_FLAG_ARRAY || (code&M32_VARTYPE_MASK)==M32_FLAG_STRUCT)
                    {
                        if (code&M32_FLAG_CONSTANT)
                            Bsprintf(buf2, "%d", (code>>16)&0xffff);
                        else
                        {
                            char *label = aGameVars[(code>>16)&(MAXGAMEVARS-1)].szLabel;
                            Bsprintf(buf2, "%s", label?label:"???");
                        }
                    }
                    else if ((code&M32_VARTYPE_MASK)==M32_FLAG_LOCAL)
                        Bsprintf(buf2, "%d", code&(MAXGAMEVARS-1));

                    if ((code&0x0000FFFC) == M32_FLAG_CONSTANT) // addlogvar for a constant.. why not? :P
                    {
                        switch (code&3)
                        {
                        case 0: Bsprintf(buf, "(immediate constant)"); break;
                        case 1: Bsprintf(buf, "(indirect constant)"); break;
                        case 2: Bsprintf(buf, "(label constant)"); break;
                        default: Bsprintf(buf, "(??? constant)"); break;
                        }
                    }
                    else
                    {
                        switch (code&M32_VARTYPE_MASK)
                        {
                        case M32_FLAG_ARRAY:
                            Bsprintf(buf, "%s[%s]", aGameArrays[code&(MAXGAMEARRAYS-1)].szLabel?
                                     aGameArrays[code&(MAXGAMEARRAYS-1)].szLabel:"???", buf2);
                            break;
                        case M32_FLAG_STRUCT:
                        {
                            int32_t memberid=(code>>2)&63, lightp = (memberid >= LIGHT_X);
                            const char *pp1[4] = {"sprite","sector","wall","tsprite"};
                            const memberlabel_t *pp2[4] = {SpriteLabels, SectorLabels, WallLabels, SpriteLabels};
                            if (lightp)
                            {
                                pp1[3] = "light";
                                pp2[3] = LightLabels;
                                memberid -= LIGHT_X;
                            }

                            Bsprintf(buf, "%s[%s].%s", pp1[code&3], buf2, pp2[code&3][memberid].name);
                        }
                        break;
                        case M32_FLAG_VAR:
                            Bsprintf(buf, "???");
                            break;
                        case M32_FLAG_LOCAL:
                            Bsprintf(buf, ".local[%s]", buf2);
                            break;
                        }
                    }
                }
                else
                {
                    if (aGameVars[code].dwFlags & GAMEVAR_PERBLOCK)
                    {
                        Bsprintf(buf2, "(%s", vm.g_st==0? "top-level) " : vm.g_st<=MAXEVENTS? "event" : "state");
                        if (vm.g_st >= 1+MAXEVENTS && vm.g_st <1+MAXEVENTS+g_stateCount)
                            Bsprintf(buf, " `%s') ", statesinfo[vm.g_st-1-MAXEVENTS].name);
                        else if (vm.g_st > 0)
                            Bsprintf(buf, " %d) ", vm.g_st-1);
                        Bstrcat(buf2, buf);
                    }

                    Bsprintf(buf, "%s%s", buf2, aGameVars[code].szLabel ? aGameVars[code].szLabel : "???");
                }

                OSD_Printf("L%d: %s%s=%d\n", g_errorLineNum, negate?"-":"", buf, val);

                insptr++;
                continue;
            }

        case CON_DEBUG:
            insptr++;
            initprintf("%d\n",*insptr++);
            continue;

// *** strings
        case CON_REDEFINEQUOTE:
            insptr++;
            {
                int32_t q = *insptr++, i = *insptr++;
                X_ERROR_INVALIDQUOTE(q, ScriptQuotes);
                X_ERROR_INVALIDQUOTE(i, ScriptQuoteRedefinitions);
                Bstrcpy(ScriptQuotes[q],ScriptQuoteRedefinitions[i]);
                continue;
            }

            insptr++;
            X_ERROR_INVALIDQUOTE(*insptr, ScriptQuotes);
            OSD_Printf("%s", ScriptQuotes[*insptr++]);
            continue;

        case CON_GETNUMBER16:  /* deprecated */
        case CON_GETNUMBER256:  /* deprecated */
        case CON_GETNUMBERFROMUSER:
            insptr++;
            {
                int32_t var=*insptr++, quote=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(quote);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                {
                    int32_t max=Gv_GetVarX(*insptr++);
                    int32_t sign = (tw==CON_GETNUMBERFROMUSER) ? Gv_GetVarX(*insptr++) : (max<=0);
                    char buf[64];  // buffers in getnumber* are 80 bytes long

                    Bstrncpyz(buf, quotetext, sizeof(buf));

                    if (max==0)
                        max = INT32_MAX;
                    else
                        max = klabs(max);

//OSD_Printf("max:%d, sign:%d\n", max, sign);
                    if (tw==CON_GETNUMBERFROMUSER)
                    {
                        Gv_SetVarX(var, in3dmode() ?
                                   getnumber256(quotetext, Gv_GetVarX(var), max, sign) :
                                   getnumber16(quotetext, Gv_GetVarX(var), max, sign));
                    }
                    else if (tw==CON_GETNUMBER16)
                        Gv_SetVarX(var, getnumber16(quotetext, Gv_GetVarX(var), max, sign));
                    else
                        Gv_SetVarX(var, getnumber256(quotetext, Gv_GetVarX(var), max, sign));
                }
            }
            continue;

        case CON_PRINT:
        case CON_QUOTE:
        case CON_ERRORINS:
        case CON_PRINTMESSAGE16:
        case CON_PRINTMESSAGE256:
        case CON_PRINTEXT256:
        case CON_PRINTEXT16:
        case CON_DRAWLABEL:
            insptr++;
            {
                int32_t i=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(i);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                {
                    int32_t x=(tw>=CON_PRINTMESSAGE256)?Gv_GetVarX(*insptr++):0;
                    int32_t y=(tw>=CON_PRINTMESSAGE256)?Gv_GetVarX(*insptr++):0;

                    int32_t col=(tw>=CON_PRINTEXT256)?Gv_GetVarX(*insptr++):0;
                    int32_t backcol=(tw>=CON_PRINTEXT256)?Gv_GetVarX(*insptr++):0;
                    int32_t fontsize=(tw>=CON_PRINTEXT256)?Gv_GetVarX(*insptr++):0;

                    if (tw==CON_PRINT || tw==CON_ERRORINS)
                    {
                        OSD_Printf("%s\n", quotetext);
                        if (tw==CON_ERRORINS)
                            vm.flags |= VMFLAG_ERROR;
                    }
                    else if (tw==CON_QUOTE)
                    {
                        message("%s", quotetext);
                    }
                    else if (tw==CON_PRINTMESSAGE16)
                    {
                        if (!in3dmode())
                            printmessage16("%s", quotetext);
                    }
                    else if (tw==CON_PRINTMESSAGE256)
                    {
                        if (in3dmode())
                            printmessage256(x, y, quotetext);
                    }
                    else if (tw==CON_PRINTEXT256)
                    {
                        if (in3dmode())
                        {
                            if (col>=256)
                                col=0;
                            else if (col < 0 && col >= -255)
                                col = editorcolors[-col];

                            if (backcol<0 || backcol>=256)
                                backcol=-1;

                            printext256(x, y, col, backcol, quotetext, fontsize);
                        }
                    }
                    else if (tw==CON_PRINTEXT16)
                    {
                        if (!in3dmode())
                            printext16(x, y, editorcolors[col&255], backcol<0 ? -1 : editorcolors[backcol&255],
                                       quotetext, fontsize);
                    }
                    else if (tw==CON_DRAWLABEL)
                    {
                        if (!in3dmode())
                        {
                            drawsmallabel(quotetext,
                                          editorcolors[backcol&255],  // col
                                          fontsize < 0 ? -1 : editorcolors[fontsize&255], editorcolors[fontsize&255] - 3, // backcol
                                          x, y, col);  // x y z
                        }
                    }
                }
            }
            continue;

        case CON_QSTRLEN:
            insptr++;
            {
                int32_t i=*insptr++, quote=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(quote);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                Gv_SetVarX(i, Bstrlen(quotetext));
                continue;
            }

        case CON_QSUBSTR:
            insptr++;
            {
                int32_t q1 = Gv_GetVarX(*insptr++);
                int32_t q2 = *insptr++;
                const char *q2text = GetMaybeInlineQuote(q2);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(q1, ScriptQuotes);

                {
                    int32_t st = Gv_GetVarX(*insptr++);
                    int32_t ln = Gv_GetVarX(*insptr++);
                    char *s1 = ScriptQuotes[q1];
                    const char *s2 = q2text;

                    while (*s2 && st--) s2++;
                    while ((*s1 = *s2) && ln--)
                    {
                        s1++;
                        s2++;
                    }
                    *s1=0;
                }
                continue;
            }

        case CON_QSTRNCAT:
        case CON_QSTRCAT:
        case CON_QSTRCPY:
///        case CON_QGETSYSSTR:
            insptr++;
            {
                int32_t i = Gv_GetVarX(*insptr++);
                int32_t j = *insptr++;

                const char *quotetext = GetMaybeInlineQuote(j);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(i, ScriptQuotes);

                switch (tw)
                {
                case CON_QSTRCAT:
                    Bstrncat(ScriptQuotes[i], quotetext, (MAXQUOTELEN-1)-Bstrlen(ScriptQuotes[i]));
                    break;
                case CON_QSTRNCAT:
                    Bstrncat(ScriptQuotes[i], quotetext, Gv_GetVarX(*insptr++));
                    break;
                case CON_QSTRCPY:
                    Bstrcpy(ScriptQuotes[i], quotetext);
                    break;
                }
                continue;
            }

        case CON_QSPRINTF:
            insptr++;
            {
                int32_t dq=Gv_GetVarX(*insptr++), sq=*insptr++;
                const char *sourcetext = GetMaybeInlineQuote(sq);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(dq, ScriptQuotes);

                {
                    int32_t arg[32], numvals=0, i=0, j=0, k=0;
                    int32_t len = Bstrlen(sourcetext);
                    char tmpbuf[MAXQUOTELEN<<1];

                    while (*insptr != -1 && numvals < 32)
                        arg[numvals++] = Gv_GetVarX(*insptr++);

                    insptr++; // skip the NOP

                    i = 0;
                    do
                    {
                        while (k < len && j < MAXQUOTELEN && sourcetext[k] != '%')
                            tmpbuf[j++] = sourcetext[k++];

                        if (sourcetext[k] == '%')
                        {
                            k++;

                            if (i>=numvals) goto dodefault;

                            switch (sourcetext[k])
                            {
                            case 'l':
                                if (sourcetext[k+1] != 'd')
                                {
                                    // write the % and l
                                    tmpbuf[j++] = sourcetext[k-1];
                                    tmpbuf[j++] = sourcetext[k++];
                                    break;
                                }
                                k++;
                            case 'd':
                            {
                                char buf[16];
                                int32_t ii = 0;

                                Bsprintf(buf, "%d", arg[i++]);

                                ii = Bstrlen(buf);
                                Bmemcpy(&tmpbuf[j], buf, ii);
                                j += ii;
                                k++;
                            }
                            break;

                            case 'f':
                            {
                                char buf[64];
                                int32_t ii = 0;

                                Bsprintf(buf, "%f", *((float *)&arg[i++]));

                                ii = Bstrlen(buf);
                                Bmemcpy(&tmpbuf[j], buf, ii);
                                j += ii;
                                k++;
                            }
                            break;

                            case 's':
                            {
                                if (arg[i]>=0 && arg[i]<MAXQUOTES && ScriptQuotes[arg[i]])
                                {
                                    int32_t ii = Bstrlen(ScriptQuotes[arg[i]]);
                                    Bmemcpy(&tmpbuf[j], ScriptQuotes[arg[i]], ii);
                                    j += ii;
                                }
                                k++;
                            }
                            break;

dodefault:
                            default:
                                tmpbuf[j++] = sourcetext[k-1];
                                break;
                            }
                        }
                    }
                    while (k < len && j < MAXQUOTELEN);

                    tmpbuf[j] = '\0';
                    Bmemcpy(ScriptQuotes[dq], tmpbuf, MAXQUOTELEN);
                    ScriptQuotes[dq][MAXQUOTELEN-1] = '\0';
                    continue;
                }
            }

// *** findnear*
// CURSPR vvv
        case CON_FINDNEARSPRITE:
        case CON_FINDNEARSPRITE3D:
        case CON_FINDNEARSPRITEVAR:
        case CON_FINDNEARSPRITE3DVAR:
            insptr++;
            {
                // syntax findnearactor(var) <type> <maxdist(var)> <getvar>
                // gets the sprite ID of the nearest actor within max dist
                // that is of <type> into <getvar>
                // -1 for none found
                // <type> <maxdist(varid)> <varid>
                int32_t lType=*insptr++;
                int32_t lMaxDist = (tw==CON_FINDNEARSPRITE || tw==CON_FINDNEARSPRITE3D)?
                                   *insptr++ : Gv_GetVarX(*insptr++);
                int32_t lVarID=*insptr++;
                int32_t lFound=-1, j, k = MAXSTATUS-1;

                X_ERROR_INVALIDCI();
                do
                {
                    j=headspritestat[k];    // all sprites
                    if (tw==CON_FINDNEARSPRITE3D || tw==CON_FINDNEARSPRITE3DVAR)
                    {
                        while (j>=0)
                        {
                            if (sprite[j].picnum == lType && j != vm.g_i && dist(&sprite[vm.g_i], &sprite[j]) < lMaxDist)
                            {
                                lFound=j;
                                j = MAXSPRITES;
                                break;
                            }
                            j = nextspritestat[j];
                        }
                        if (j == MAXSPRITES)
                            break;
                        continue;
                    }

                    while (j>=0)
                    {
                        if (sprite[j].picnum == lType && j != vm.g_i && ldist(&sprite[vm.g_i], &sprite[j]) < lMaxDist)
                        {
                            lFound=j;
                            j = MAXSPRITES;
                            break;
                        }
                        j = nextspritestat[j];
                    }

                    if (j == MAXSPRITES)
                        break;
                }
                while (k--);
                Gv_SetVarX(lVarID, lFound);
                continue;
            }

        case CON_FINDNEARSPRITEZVAR:
        case CON_FINDNEARSPRITEZ:
            insptr++;
            {
                // syntax findnearactor(var) <type> <maxdist(var)> <getvar>
                // gets the sprite ID of the nearest actor within max dist
                // that is of <type> into <getvar>
                // -1 for none found
                // <type> <maxdist(varid)> <varid>
                int32_t lType=*insptr++;
                int32_t lMaxDist = (tw==CON_FINDNEARSPRITEZVAR) ? Gv_GetVarX(*insptr++) : *insptr++;
                int32_t lMaxZDist = (tw==CON_FINDNEARSPRITEZVAR) ? Gv_GetVarX(*insptr++) : *insptr++;
                int32_t lVarID=*insptr++;
                int32_t lFound=-1, lTemp, lTemp2, j, k=MAXSTATUS-1;

                X_ERROR_INVALIDCI();
                do
                {
                    j=headspritestat[k];    // all sprites
                    if (j == -1) continue;
                    do
                    {
                        if (sprite[j].picnum == lType && j != vm.g_i)
                        {
                            lTemp=ldist(&sprite[vm.g_i], &sprite[j]);
                            if (lTemp < lMaxDist)
                            {
                                lTemp2=klabs(sprite[vm.g_i].z-sprite[j].z);
                                if (lTemp2 < lMaxZDist)
                                {
                                    lFound=j;
                                    j = MAXSPRITES;
                                    break;
                                }
                            }
                        }
                        j = nextspritestat[j];
                    }
                    while (j>=0);
                    if (j == MAXSPRITES)
                        break;
                }
                while (k--);
                Gv_SetVarX(lVarID, lFound);

                continue;
            }
// ^^^

        case CON_GETTICKS:
            insptr++;
            {
                int32_t j=*insptr++;
                Gv_SetVarX(j, getticks());
            }
            continue;

        case CON_SETASPECT:
            insptr++;
            {
                int32_t daxrange = Gv_GetVarX(*insptr++), dayxaspect = Gv_GetVarX(*insptr++);
                if (daxrange < (1<<12)) daxrange = (1<<12);
                if (daxrange > (1<<20)) daxrange = (1<<20);
                if (dayxaspect < (1<<12)) dayxaspect = (1<<12);
                if (dayxaspect > (1<<20)) dayxaspect = (1<<20);
                setaspect(daxrange, dayxaspect);
                continue;
            }

// vvv CURSPR
        case CON_SETI:
        {
            int32_t newcurspritei;

            insptr++;
            newcurspritei = Gv_GetVarX(*insptr++);
            X_ERROR_INVALIDSPRI(newcurspritei);
            vm.g_i = newcurspritei;
            vm.g_sp = (tspritetype *)&sprite[vm.g_i];
            continue;
        }

        case CON_SIZEAT:
            insptr += 3;
            X_ERROR_INVALIDSP();
            vm.g_sp->xrepeat = (uint8_t) Gv_GetVarX(*(insptr-2));
            vm.g_sp->yrepeat = (uint8_t) Gv_GetVarX(*(insptr-1));
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_CSTAT:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.g_sp->cstat = (int16_t) *(insptr-1);
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_CSTATOR:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.g_sp->cstat |= (int16_t) Gv_GetVarX(*(insptr-1));
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_CLIPDIST:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.g_sp->clipdist = (uint8_t) Gv_GetVarX(*(insptr-1));
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_SPRITEPAL:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.g_sp->pal = Gv_GetVarX(*(insptr-1));
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_CACTOR:
            insptr += 2;
            X_ERROR_INVALIDSP();
            vm.g_sp->picnum = Gv_GetVarX(*(insptr-1));
            if (vm.g_i != -1) spritechanged[vm.g_i]++;
            continue;

        case CON_SPGETLOTAG:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_LOTAG_VAR_ID, vm.g_sp->lotag);
            continue;

        case CON_SPGETHITAG:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_HITAG_VAR_ID, vm.g_sp->hitag);
            continue;

        case CON_SECTGETLOTAG:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_LOTAG_VAR_ID, sector[vm.g_sp->sectnum].lotag);
            continue;

        case CON_SECTGETHITAG:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_HITAG_VAR_ID, sector[vm.g_sp->sectnum].hitag);
            continue;

        case CON_GETTEXTUREFLOOR:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_TEXTURE_VAR_ID, sector[vm.g_sp->sectnum].floorpicnum);
            continue;

        case CON_GETTEXTURECEILING:
            insptr++;
            X_ERROR_INVALIDSP();
            Gv_SetVarX(M32_TEXTURE_VAR_ID, sector[vm.g_sp->sectnum].ceilingpicnum);
            continue;
// ^^^
        case CON_DRAWLINE16:
        case CON_DRAWLINE16B:
        case CON_DRAWLINE16Z:
            insptr++;
            {
                int32_t x1=Gv_GetVarX(*insptr++), y1=Gv_GetVarX(*insptr++);
                int32_t z1=tw==CON_DRAWLINE16Z?Gv_GetVarX(*insptr++):0;
                int32_t x2=Gv_GetVarX(*insptr++), y2=Gv_GetVarX(*insptr++);
                int32_t z2=tw==CON_DRAWLINE16Z?Gv_GetVarX(*insptr++):0;
                int32_t col=Gv_GetVarX(*insptr++), odrawlinepat=drawlinepat;
                int32_t xofs=0, yofs=0;

                if (tw==CON_DRAWLINE16B || tw==CON_DRAWLINE16Z)
                {
                    screencoords(&x1,&y1, x1-pos.x,y1-pos.y, zoom);
                    screencoords(&x2,&y2, x2-pos.x,y2-pos.y, zoom);

                    if (tw==CON_DRAWLINE16Z && m32_sideview)
                    {
                        y1 += getscreenvdisp(z1-pos.z,zoom);
                        y2 += getscreenvdisp(z2-pos.z,zoom);
                    }

                    xofs = halfxdim16;
                    yofs = midydim16;
                }

                drawlinepat = m32_drawlinepat;
                drawline16(xofs+x1,yofs+y1, xofs+x2,yofs+y2, col>=0?editorcolors[col&15]:((-col)&255));
                drawlinepat = odrawlinepat;
                continue;
            }

        case CON_DRAWCIRCLE16:
        case CON_DRAWCIRCLE16B:
        case CON_DRAWCIRCLE16Z:
            insptr++;
            {
                int32_t x1=Gv_GetVarX(*insptr++), y1=Gv_GetVarX(*insptr++);
                int32_t z1 = tw==CON_DRAWCIRCLE16Z ? Gv_GetVarX(*insptr++) : 0;
                int32_t r=Gv_GetVarX(*insptr++);
                int32_t col=Gv_GetVarX(*insptr++), odrawlinepat=drawlinepat;
                int32_t xofs=0, yofs=0, eccen=16384;

                if (tw==CON_DRAWCIRCLE16B || tw==CON_DRAWCIRCLE16Z)
                {
                    screencoords(&x1,&y1, x1-pos.x,y1-pos.y, zoom);
                    if (m32_sideview)
                        y1 += getscreenvdisp(z1-pos.z, zoom);
                    r = mulscale14(r,zoom);
                    eccen = scalescreeny(eccen);
                    xofs = halfxdim16;
                    yofs = midydim16;
                }

                drawlinepat = m32_drawlinepat;
                drawcircle16(xofs+x1, yofs+y1, r, eccen, col>=0?editorcolors[col&15]:((-col)&255));
                drawlinepat = odrawlinepat;
                continue;
            }

        case CON_ROTATESPRITEA:
        case CON_ROTATESPRITE16:
        case CON_ROTATESPRITE:
            insptr++;
            {
                int32_t x=Gv_GetVarX(*insptr++),   y=Gv_GetVarX(*insptr++),           z=Gv_GetVarX(*insptr++);
                int32_t a=Gv_GetVarX(*insptr++),   tilenum=Gv_GetVarX(*insptr++),     shade=Gv_GetVarX(*insptr++);
                int32_t pal=Gv_GetVarX(*insptr++), orientation=Gv_GetVarX(*insptr++);
                int32_t alpha = (tw == CON_ROTATESPRITEA) ? Gv_GetVarX(*insptr++) : 0;
                int32_t x1=Gv_GetVarX(*insptr++),  y1=Gv_GetVarX(*insptr++);
                int32_t x2=Gv_GetVarX(*insptr++),  y2=Gv_GetVarX(*insptr++);

                if (tw != CON_ROTATESPRITE16 && !(orientation&ROTATESPRITE_FULL16))
                {
                    x<<=16;
                    y<<=16;
                }

                // NOTE: Unlike CON, no error on large x/y.

                orientation &= (ROTATESPRITE_MAX-1);

                rotatesprite_(x,y,z,a,tilenum,shade,pal,2|orientation,alpha,0,x1,y1,x2,y2);
                continue;
            }

        case CON_SETGAMEPALETTE:
            insptr++;
            SetGamePalette(Gv_GetVarX(*insptr++));
            continue;

// *** sounds
        case CON_IFSOUND:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr);
                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    insptr++;
                    continue;
                }
                VM_DoConditional(S_CheckSoundPlaying(vm.g_i,j));
            }
            continue;

        case CON_IFNOSOUNDS:
            VM_DoConditional(S_SoundsPlaying(vm.g_i) < 0);
        continue;

        case CON_IFIN3DMODE:
            VM_DoConditional(in3dmode());
            continue;

        // ifaimingsprite and -wall also work in 2d mode, but you must "and" with 16383 yourself
        case CON_IFAIMINGSPRITE:
            VM_DoConditional(AIMING_AT_SPRITE || (!in3dmode() && pointhighlight>=16384));
            continue;
        case CON_IFAIMINGWALL:
            VM_DoConditional(AIMING_AT_WALL_OR_MASK || (!in3dmode() && linehighlight>=0));
            continue;
        case CON_IFAIMINGSECTOR:
            VM_DoConditional(AIMING_AT_CEILING_OR_FLOOR);
            continue;
        case CON_IFINTERACTIVE:
            VM_DoConditional(vm.miscflags&VMFLAG_MISC_INTERACTIVE);
            continue;

        case CON_GETSOUNDFLAGS:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++), var=*insptr++;
                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    insptr++;
                    continue;
                }

                Gv_SetVarX(var, S_SoundFlags(j));
            }
            continue;

        case CON_SOUNDVAR:
        case CON_STOPSOUNDVAR:
        case CON_SOUNDONCEVAR:
        case CON_GLOBALSOUNDVAR:
            insptr++;
            {
                int32_t j=Gv_GetVarX(*insptr++);

                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    continue;
                }

                switch (tw)
                {
                case CON_SOUNDONCEVAR:
                    if (!S_CheckSoundPlaying(vm.g_i,j))
                        A_PlaySound((int16_t)j,vm.g_i);
                    break;
                case CON_GLOBALSOUNDVAR:
                    A_PlaySound((int16_t)j,-1);
                    break;
                case CON_STOPSOUNDVAR:
                    if (S_CheckSoundPlaying(vm.g_i,j))
                        S_StopSound((int16_t)j);
                    break;
                case CON_SOUNDVAR:
                    A_PlaySound((int16_t)j,vm.g_i);
                    break;
                }
            }
            continue;

        case CON_STOPALLSOUNDS:
            insptr++;
            FX_StopAllSounds();
            continue;

        default:
            VM_ScriptInfo();

            OSD_Printf("\nAn error has occurred in the Mapster32 virtual machine.\n\n"
                       "Please e-mail the file mapster32.log along with every M32 file\n"
                       "you're using and instructions how to reproduce this error to\n"
                       "helixhorned@gmail.com.\n\n"
                       "Thank you!\n");
            vm.flags |= VMFLAG_ERROR;
            Bfflush(NULL);
            return 1;
        }
    }

    return 0;
}
