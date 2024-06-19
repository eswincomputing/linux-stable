struct SubsysDesc subsys_array[] ={
    {0, 0, 0x50100000},
    {0, 1, 0x50120000},
//  {0, 2, 0x60100000},
//  {0, 3, 0x60120000},
};

struct CoreDesc core_array[] ={
/* slice_id, subsystem_id,  core_type,   offset,               iosize,         irq, has_apb */
//    {0,             0,      HW_VCMD,        0x0,               27 * 4,          232,     0},
    {0,             0,      HW_AXIFE,    0x0200,                 64 * 4,          -1,     0},
    {0,             0,      HW_VC8000D, 0x0800,      MAX_REG_COUNT * 4,          -1,     0},
//    {0,             1,      HW_VCMD,        0x0,               27 * 4,          233,     0},
    {0,             1,      HW_AXIFE,    0x0200,                 64 * 4,          -1,     0},
    {0,             1,      HW_VC8000D,  0x0800,      MAX_REG_COUNT * 4,          -1,     0},
};
