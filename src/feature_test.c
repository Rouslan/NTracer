#include <stdio.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

static const char *mode10_edx[32] = {
"fpu",
"vme",
"de",
"pse",
"tsc",
"msr",
"pae",
"mce",
"cx8",
"apic",
"",
"sep",
"mtrr",
"pge",
"mca",
"cmov",
"pat",
"pse-36",
"psn",
"clfsh",
"",
"ds",
"acpi",
"mmx",
"fxsr",
"sse",
"sse2",
"ss",
"htt",
"tm",
"ia64",
"pbe"};

static const char *mode10_ecx[32] = {
"sse3",
"pclmulqdq",
"dtes64",
"monitor",
"ds-cpl",
"vmx",
"smx",
"est",
"tm2",
"ssse3",
"cnxt-id",
"sdbg",
"fma",
"cx16",
"xtpr",
"pdcm",
"",
"pcid",
"dca",
"sse4.1",
"sse4.2",
"x2apic",
"movbe",
"popcnt",
"tsc-deadline",
"aes",
"xsave",
"osxsave",
"avx",
"f16c",
"rdrnd",
"hypervisor"};

static const char *mode70_ebx[32] = {
"fsgsbase",
"", /* described as IA32_TSC_ADJUST, I didn't find a name */
"sgx",
"bmi1",
"hle",
"avx2",
"", /* described as FDP_EXCPTN_ONLY, I didn't find a name */
"smep",
"bmi2",
"erms",
"invpcid",
"rtm",
"pqm",
"",
"mpx",
"pqe",
"avx512_f",
"avx512_dq",
"rdseed",
"adx",
"smap",
"avx512_ifma",
"pcommit",
"clflushopt",
"clwb",
"intel_pt",
"avx512_pf",
"avx512_er",
"avx512_cd",
"sha",
"avx512_bw",
"avx512_vl"};

static const char *mode70_ecx[32] = {
"prefetchwt1",
"avx512_vbmi",
"umip",
"pku",
"ospke",
"waitpkg",
"avx512_vbmi2",
"cet_ss",
"gfni",
"vaes",
"vpclmulqdq",
"avx512_vnni",
"avx512_bitalg",
"",
"avx512_vpopcntdq",
"",
"", /* described as 5-level paging, I didn't find a name */
"mawau",
"",
"",
"",
"",
"rdpid",
"",
"",
"cldemote",
"",
"MOVDIRI",
"MOVDIR64B",
"ENQCMD",
"sgx_lc",
"pks"};

static const char *mode70_edx[32] = {
"",
"",
"avx512_4vnniw",
"avx512_4fmaps",
"fsrm",
"",
"",
"",
"avx512_vp2intersect",
"SRBDS_CTRL",
"md_clear",
"",
"",
"tsx_force_abort",
"SERIALIZE",
"Hybrid",
"TSXLDTRK",
"",
"pconfig",
"lbr",
"cet_ibt",
"",
"amx-bf16",
"",
"amx-tile",
"amx-int8",
"IBRS_IBPB/spec_ctrl",
"stibp ",
"L1D_FLUSH",
"IA32_ARCH_CAPABILITIES",
"IA32_CORE_CAPABILITIES",
"ssbd"};

static const char *mode71_eax[32] = {
"",
"",
"",
"",
"",
"avx512_bf16",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
"",
""};

static void test_bits(const char **names,unsigned int val) {
    unsigned int i;
    for(i=0; i<32; ++i) {
        if(names[i][0] && (val & 1)) {
            printf(names[i]);
            printf("\n");
        }
        val >>= 1;
    }
}

struct reg_set {
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;
};

static void call_cpuid(int code,int subcode,struct reg_set *regs) {
#ifdef _MSC_VER
    int cpuid[4];
    __cpuidex(cpuid,code,subcode);
    regs->a = cpuid[0];
    regs->b = cpuid[1];
    regs->c = cpuid[2];
    regs->d = cpuid[3];
#else
    asm ("cpuid" : "=a" (regs->a), "=b" (regs->b), "=c" (regs->c), "=d" (regs->d) : "a" (code), "c" (subcode));
#endif
}

int main(void) {
    int highest_id;
    struct reg_set regs;

    call_cpuid(0,0,&regs);
    highest_id = regs.a;

    call_cpuid(1,0,&regs);
    test_bits(mode10_edx,regs.d);
    test_bits(mode10_ecx,regs.c);

    if(highest_id >= 7) {
        call_cpuid(7,0,&regs);
        test_bits(mode70_ebx,regs.b);
        test_bits(mode70_ecx,regs.c);
        test_bits(mode70_edx,regs.d);

        call_cpuid(7,1,&regs);
        test_bits(mode71_eax,regs.a);
    }

    return 0;
}
