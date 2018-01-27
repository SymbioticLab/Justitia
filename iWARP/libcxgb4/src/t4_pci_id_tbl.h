/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver.
 *
 * Copyright (C) 2003-2017 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */
#ifndef __T4_PCI_ID_TBL_H__
#define __T4_PCI_ID_TBL_H__

/*
 * The Os-Dependent code can defined cpp macros for creating a PCI Device ID
 * Table.  This is useful because it allows the PCI ID Table to be maintained
 * in a single place and all supporting OSes to get new PCI Device IDs
 * automatically.
 *
 * The macros are:
 *
 * CH_PCI_DEVICE_ID_TABLE_DEFINE_BEGIN
 *   -- Used to start the definition of the PCI ID Table.
 *
 * CH_PCI_DEVICE_ID_FUNCTION
 *   -- The PCI Function Number to use in the PCI Device ID Table.  "0"
 *   -- for drivers attaching to PF0-3, "4" for drivers attaching to PF4,
 *   -- "8" for drivers attaching to SR-IOV Virtual Functions, etc.
 *
 * CH_PCI_DEVICE_ID_FUNCTION2 [optional]
 *   -- If defined, create a PCI Device ID Table with both
 *   -- CH_PCI_DEVICE_ID_FUNCTION and CH_PCI_DEVICE_ID_FUNCTION2 populated.
 *
 * CH_PCI_ID_TABLE_ENTRY(DeviceID)
 *   -- Used for the individual PCI Device ID entries.  Note that we will
 *   -- be adding a trailing comma (",") after all of the entries (and
 *   -- between the pairs of entries if CH_PCI_DEVICE_ID_FUNCTION2 is defined).
 *
 * CH_PCI_DEVICE_ID_TABLE_DEFINE_END
 *   -- Used to finish the definition of the PCI ID Table.  Note that we
 *   -- will be adding a trailing semi-colon (";") here.
 *
 * CH_PCI_DEVICE_ID_BYPASS_SUPPORTED [optional]
 *   -- If defined, indicates that the OS Driver has support for Bypass
 *   -- Adapters.
 */

/*
 * Some sanity checks ...
 */
#ifndef CH_PCI_DEVICE_ID_FUNCTION
#error CH_PCI_DEVICE_ID_FUNCTION not defined!
#endif
#ifndef CH_PCI_ID_TABLE_ENTRY
#error CH_PCI_ID_TABLE_ENTRY not defined!
#endif
#ifndef CH_PCI_DEVICE_ID_TABLE_DEFINE_END
#error CH_PCI_DEVICE_ID_TABLE_DEFINE_END not defined!
#endif

/*
 * T4 and later ASICs use a PCI Device ID scheme of 0xVFPP where:
 *
 *   V  = "4" for T4; "5" for T5, etc.
 *   F  = "0" for PF 0..3; "4".."7" for PF4..7; and "8" for VFs
 *   PP = adapter product designation
 *
 * We use this consistency in order to create the proper PCI Device IDs
 * for the specified CH_PCI_DEVICE_ID_FUNCTION.
 */
#ifndef CH_PCI_DEVICE_ID_FUNCTION2
#define CH_PCI_ID_TABLE_FENTRY(__DeviceID) \
	CH_PCI_ID_TABLE_ENTRY((__DeviceID) | \
			      ((CH_PCI_DEVICE_ID_FUNCTION) << 8))
#else
#define CH_PCI_ID_TABLE_FENTRY(__DeviceID) \
	CH_PCI_ID_TABLE_ENTRY((__DeviceID) | \
			      ((CH_PCI_DEVICE_ID_FUNCTION) << 8)), \
	CH_PCI_ID_TABLE_ENTRY((__DeviceID) | \
			      ((CH_PCI_DEVICE_ID_FUNCTION2) << 8))
#endif

/* Note : The comments against each entry are used by the scripts in the vmware drivers
 * to correctly generate the pciid xml file, do not change the format currently used.
 */

CH_PCI_DEVICE_ID_TABLE_DEFINE_BEGIN
	/*
	 * FPGAs:
	 *
	 * Unfortunately the FPGA PCI Device IDs don't follow the ASIC PCI
	 * Device ID numbering convetions for the Physical Functions.
	 */
#if CH_PCI_DEVICE_ID_FUNCTION != 8
	CH_PCI_ID_TABLE_ENTRY(0xa000),	/* PE10K FPGA */
	CH_PCI_ID_TABLE_ENTRY(0xb000),	/* PF0 T5 PE10K5 FPGA */
	CH_PCI_ID_TABLE_ENTRY(0xb001),	/* PF0 T5 PE10K FPGA */
	CH_PCI_ID_TABLE_ENTRY(0xc006),  /* PF0 T6 PE10K6 FPGA */
#else
	CH_PCI_ID_TABLE_FENTRY(0xa000),	/* PE10K FPGA */
	CH_PCI_ID_TABLE_FENTRY(0xb000),	/* PF0 T5 PE10K5 FPGA */
	CH_PCI_ID_TABLE_FENTRY(0xb001),	/* PF0 T5 PE10K FPGA */
	CH_PCI_ID_TABLE_FENTRY(0xc006), /* PF0 T6 PE10K6 FPGA */
	CH_PCI_ID_TABLE_FENTRY(0xc106),  /* PF1 T6 PE10K6 FPGA */
#endif

	/*
	 *  These FPGAs seem to be used only by the csiostor driver
	 */
#if ((CH_PCI_DEVICE_ID_FUNCTION == 5) || (CH_PCI_DEVICE_ID_FUNCTION == 6))
	CH_PCI_ID_TABLE_ENTRY(0xa001),	/* PF1 PE10K FPGA FCOE */
	CH_PCI_ID_TABLE_ENTRY(0xa002),	/* PE10K FPGA iSCSI */
	CH_PCI_ID_TABLE_ENTRY(0xc106),  /* PF1 T6 PE10K6 FPGA */
#endif

	/*
	 * T4 adapters:
	 */
	CH_PCI_ID_TABLE_FENTRY(0x4000),	/* T440-dbg */
	CH_PCI_ID_TABLE_FENTRY(0x4001),	/* T420-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4002),	/* T422-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4003),	/* T440-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4004),	/* T420-bch */
	CH_PCI_ID_TABLE_FENTRY(0x4005),	/* T440-bch */
	CH_PCI_ID_TABLE_FENTRY(0x4006),	/* T440-ch */
	CH_PCI_ID_TABLE_FENTRY(0x4007),	/* T420-so */
	CH_PCI_ID_TABLE_FENTRY(0x4008),	/* T420-cx */
	CH_PCI_ID_TABLE_FENTRY(0x4009),	/* T420-bt */
	CH_PCI_ID_TABLE_FENTRY(0x400a),	/* T404-bt */
#ifdef CH_PCI_DEVICE_ID_BYPASS_SUPPORTED
	CH_PCI_ID_TABLE_FENTRY(0x400b),	/* B420-sr */
	CH_PCI_ID_TABLE_FENTRY(0x400c),	/* B404-bt */
#endif
	CH_PCI_ID_TABLE_FENTRY(0x400d),	/* T480-cr */
	CH_PCI_ID_TABLE_FENTRY(0x400e),	/* T440-LP-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4080),	/* Custom T480-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4081),	/* Custom T440-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4082),	/* Custom T420-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4083),	/* Custom T420-xaui */
	CH_PCI_ID_TABLE_FENTRY(0x4084),	/* Custom T440-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4085),	/* Custom T420-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4086),	/* Custom T440-bt */
	CH_PCI_ID_TABLE_FENTRY(0x4087),	/* Custom T440-cr */
	CH_PCI_ID_TABLE_FENTRY(0x4088),	/* Custom T440 2-xaui, 2-xfi */

	/*
	 * T5 adapters:
	 */
	CH_PCI_ID_TABLE_FENTRY(0x5000),	/* T580-dbg */
	CH_PCI_ID_TABLE_FENTRY(0x5001),	/* T520-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5002),	/* T522-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5003),	/* T540-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5004),	/* T520-bch */
	CH_PCI_ID_TABLE_FENTRY(0x5005),	/* T540-bch */
	CH_PCI_ID_TABLE_FENTRY(0x5006),	/* T540-ch */
	CH_PCI_ID_TABLE_FENTRY(0x5007),	/* T520-so */
	CH_PCI_ID_TABLE_FENTRY(0x5008),	/* T520-cx */
	CH_PCI_ID_TABLE_FENTRY(0x5009),	/* T520-bt */
	CH_PCI_ID_TABLE_FENTRY(0x500a),	/* T504-bt */
#ifdef CH_PCI_DEVICE_ID_BYPASS_SUPPORTED
	CH_PCI_ID_TABLE_FENTRY(0x500b),	/* B520-sr */
	CH_PCI_ID_TABLE_FENTRY(0x500c),	/* B504-bt */
#endif
	CH_PCI_ID_TABLE_FENTRY(0x500d),	/* T580-cr */
	CH_PCI_ID_TABLE_FENTRY(0x500e),	/* T540-LP-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5010),	/* T580-LP-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5011),	/* T520-LL-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5012),	/* T560-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5013),	/* T580-chr */
	CH_PCI_ID_TABLE_FENTRY(0x5014),	/* T580-so */
	CH_PCI_ID_TABLE_FENTRY(0x5015),	/* T502-bt */
	CH_PCI_ID_TABLE_FENTRY(0x5016),	/* T580-OCP-SO */
	CH_PCI_ID_TABLE_FENTRY(0x5017),	/* T520-OCP-SO */
	CH_PCI_ID_TABLE_FENTRY(0x5018),	/* T540-BT */
	CH_PCI_ID_TABLE_FENTRY(0x5080),	/* Custom T540-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5081),	/* Custom T540-LL-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5082),	/* Custom T504-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5083),	/* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5084),	/* Custom T580-cr */
	CH_PCI_ID_TABLE_FENTRY(0x5085),	/* Custom 3x T580-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5086),	/* Custom 2x T580-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5087),	/* Custom T580-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5088),	/* Custom T570-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5089),	/* Custom T520-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5090),	/* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5091),	/* Custom T522-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5092),	/* Custom T520-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5093),	/* Custom SECA */
	CH_PCI_ID_TABLE_FENTRY(0x5094),	/* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5095),	/* Custom T540-CR-SO */
	CH_PCI_ID_TABLE_FENTRY(0x5096), /* Custom T580-CR */
	CH_PCI_ID_TABLE_FENTRY(0x5097), /* Custom T520-KR */
	CH_PCI_ID_TABLE_FENTRY(0x5098), /* Custom 2x40G QSFP */
	CH_PCI_ID_TABLE_FENTRY(0x5099), /* Custom 2x40G QSFP */
	CH_PCI_ID_TABLE_FENTRY(0x509A), /* Custom T520-CR */
	CH_PCI_ID_TABLE_FENTRY(0x509B), /* Custom T540-CR LOM */
	CH_PCI_ID_TABLE_FENTRY(0x509c), /* Custom T520-CR SFP+ LOM */
	CH_PCI_ID_TABLE_FENTRY(0x509d), /* Custom T540-CR SFP+ */
	CH_PCI_ID_TABLE_FENTRY(0x509e), /* Custom T520-CR */
	CH_PCI_ID_TABLE_FENTRY(0x509f), /* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x50a0), /* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x50a1), /* Custom T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x50a2), /* Custom T580-KR4 */
	CH_PCI_ID_TABLE_FENTRY(0x50a3), /* Custom T580-KR4 */
	CH_PCI_ID_TABLE_FENTRY(0x50a4), /* Custom 2x T540-CR */
	CH_PCI_ID_TABLE_FENTRY(0x50a5), /* Custom T522-BT */
	CH_PCI_ID_TABLE_FENTRY(0x50a6), /* Custom T522-BT-SO */
	CH_PCI_ID_TABLE_FENTRY(0x50a7), /* Custom T580-CR */
	CH_PCI_ID_TABLE_FENTRY(0x50a8), /* Custom T580-KR */
	CH_PCI_ID_TABLE_FENTRY(0x50a9), /* Custom T580-KR */

	/* T6 adapter */
	CH_PCI_ID_TABLE_FENTRY(0x6000),	/* T6-DBG-25 */
	CH_PCI_ID_TABLE_FENTRY(0x6001),	/* T6225-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6002),	/* T6225-SO-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6003),	/* T6425-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6004),	/* T6425-SO-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6005),	/* T6225-OCP-SO */
	CH_PCI_ID_TABLE_FENTRY(0x6006),	/* T62100-OCP-SO */
	CH_PCI_ID_TABLE_FENTRY(0x6007),	/* T62100-LP-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6008),	/* T62100-SO-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6009),	/* T6210-BT */
	CH_PCI_ID_TABLE_FENTRY(0x600d),	/* T62100-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6010),	/* T6-DBG-100 */
	CH_PCI_ID_TABLE_FENTRY(0x6011),	/* T6225-LL-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6014),	/* T61100-OCP-SO */
	CH_PCI_ID_TABLE_FENTRY(0x6015),	/* T6201-BT */
	CH_PCI_ID_TABLE_FENTRY(0x6080), /* Custom T6225-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6081),	/* Custom T62100-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6082),	/* Custom T6225-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6083),	/* Custom T62100-CR */
	CH_PCI_ID_TABLE_FENTRY(0x6084),	/* Custom T64100-CR */
CH_PCI_DEVICE_ID_TABLE_DEFINE_END;

#endif /* __T4_PCI_ID_TBL_H__ */
