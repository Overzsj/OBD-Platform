#pragma once
#ifndef OBD_AUTO_ADAPT
#define OBD_AUTO_ADAPT 0
#endif
#ifndef OBD_DRIVE_REPLAY
#define OBD_DRIVE_REPLAY 0
#endif
#ifndef OBD_DATA_TEST
#define OBD_DATA_TEST 0
#endif
#if OBD_DATA_TEST && (!defined(OBD_TRANSPORT) || OBD_TRANSPORT != 1)
#error Data Lab requires the on-chip FAKE transport
#endif
#if OBD_DATA_TEST || (defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM)
#define OBD_BENCH_AUX 1
#else
#define OBD_BENCH_AUX 0
#endif
namespace obd {
#if OBD_DRIVE_REPLAY || OBD_DATA_TEST || (defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM) || (defined(OBD_TRANSPORT) && OBD_TRANSPORT != 0)
constexpr bool kSimulatedSource=true;
#else
constexpr bool kSimulatedSource=false;
#endif
}
