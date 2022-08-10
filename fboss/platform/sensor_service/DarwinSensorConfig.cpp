// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
// All Rights Reserved.

#include <string>

namespace facebook::fboss::platform::sensor_service {

/* ToDo: replace hardcoded sysfs path with dynamically mapped symbolic path */

std::string getDarwinConfig() {
  return R"({
  "source" : "sysfs",
  "sensorMapList" : {
    "CPU_CARD" : {
      "PCH_TEMP" :{
        "path" : "/run/devmap/sensors/PCH_THERMAL/temp1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "CPU_PHYS_ID_0" : {
        "path" : "/run/devmap/sensors/CPU_CORE_TEMP/temp1_input",
        "thresholdMap" : {
          "4" : 105
        },
        "compute" : "@/1000.0",
        "type"  : 3
      },
      "CPU_CORE0_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_CORE_TEMP/temp2_input",
        "thresholdMap" : {
          "4" : 105
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "CPU_CORE1_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_CORE_TEMP/temp3_input",
        "thresholdMap" : {
          "4" : 105
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "CPU_CORE2_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_CORE_TEMP/temp4_input",
        "thresholdMap" : {
          "4" : 105
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "CPU_CORE3_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_CORE_TEMP/temp5_input",
        "thresholdMap" : {
          "4" : 105
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "CPU_BOARD_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_BOARD_TEMP_MAX6658/temp1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "BACK_PANEL_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_BOARD_TEMP_MAX6658/temp2_input",
        "thresholdMap" : {
          "4" : 75
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "MPS1_VIN" : {
        "path" : "/run/devmap/sensors/CPU_MPS1_PMBUS/in1_input",
        "thresholdMap" : {
          "4" : 14,
          "5" : 9
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "MPS1_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_MPS1_PMBUS/temp1_input",
        "thresholdMap" : {
          "4" : 110
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "MPS1_IIN" : {
        "path" : "/run/devmap/sensors/CPU_MPS1_PMBUS/curr1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 2
      },
      "MPS1_IOUT" : {
        "path" : "/run/devmap/sensors/CPU_MPS1_PMBUS/curr2_input",
        "thresholdMap" : {
          "4" : 45
        },
        "compute" : "@/1000.0",
        "type" : 2
      },
      "MPS2_VIN" : {
        "path" : "/run/devmap/sensors/CPU_MPS2_PMBUS/in1_input",
        "thresholdMap" : {
          "4" : 14,
          "5" : 9
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "MPS2_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_MPS2_PMBUS/temp1_input",
        "thresholdMap" : {
          "4" : 110
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "MPS2_IIN" : {
        "path" : "/run/devmap/sensors/CPU_MPS2_PMBUS/curr1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 2
      },
      "MPS2_IOUT" : {
        "path" : "/run/devmap/sensors/CPU_MPS2_PMBUS/curr2_input",
        "thresholdMap" : {
          "4" : 35
        },
        "compute" : "@/1000.0",
        "type" : 2
      },
      "POS_1V7_VCCIN_VRRDY" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in1_input",
        "thresholdMap" : {
          "4" : 1.875,
          "5" : 1.12
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_0V6_VTT" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in2_input",
        "thresholdMap" : {
          "4" : 0.69,
          "5" : 0.51
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V2_VDDQ" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in3_input",
        "thresholdMap" : {
          "4" : 1.38,
          "5" : 1.02
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_2V5_VPP" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in4_input",
        "thresholdMap" : {
          "4" : 2.99,
          "5" : 2.21
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V5_PCH" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in5_input",
        "thresholdMap" : {
          "4" : 1.725,
          "5" : 1.27
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V05_COM" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in6_input",
        "thresholdMap" : {
          "4" : 1.208,
          "5" : 0.89
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V3_KRHV" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in7_input",
        "thresholdMap" : {
          "4" : 1.495,
          "5" : 1.1
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V7_SCFUSE" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in8_input",
        "thresholdMap" : {
          "4" : 1.955,
          "5" : 1.44
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_3V3" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in9_input",
        "thresholdMap" : {
          "4" : 3.795,
          "5" : 2.8
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_5V0" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in10_input",
        "thresholdMap" : {
          "4" : 5.75,
          "5" : 4.25
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V2_ALW" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in11_input",
        "thresholdMap" : {
          "4" : 1.38,
          "5" : 1.02
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_3V3_ALW" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in12_input",
        "thresholdMap" : {
          "4" : 3.795,
          "5" : 2.8
        },
        "type" : 1
      },
      "POS_12V" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in13_input",
        "thresholdMap" : {
          "4" : 13.8,
          "5" : 9.72
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V2_LAN1" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in14_input",
        "thresholdMap" : {
          "4" : 1.38,
          "5" : 1.02
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "POS_1V2_LAN2" : {
        "path" : "/run/devmap/sensors/CPU_POS_UCD90160/in15_input",
        "thresholdMap" : {
          "4" : 1.38,
          "5" : 1.02
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "FRONT_PANEL_TEMP" : {
        "path" : "/run/devmap/sensors/CPU_FP_TEMP_LM73/temp1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      }
    },

    "FAN1" : {
      "FAN1_RPM" : {
        "path" : "/run/devmap/sensors/FAN_CPLD/fan1_input",
        "thresholdMap" : {
          "4" : 25500,
          "5" : 2600
        },
        "type" : 4
        }
    },
    "FAN2" : {
      "FAN2_RPM" : {
        "path" : "/run/devmap/sensors/FAN_CPLD/fan2_input",
        "thresholdMap" : {
          "4" : 25500,
          "5" : 2600
        },
        "type" : 4
      }
    },
    "FAN3" : {
      "FAN3_RPM" : {
        "path" : "/run/devmap/sensors/FAN_CPLD/fan3_input",
        "thresholdMap" : {
          "4" : 25500,
          "5" : 2600
        },
        "type"  : 4
      }
    },
    "FAN4" : {
      "FAN4_RPM" : {
        "path" : "/run/devmap/sensors/FAN_CPLD/fan4_input",
        "thresholdMap" : {
          "4" : 25500,
          "5" : 2600
        },
        "type" : 4
      }
    },
    "FAN5" : {
      "FAN5_RPM" : {
        "path" : "/run/devmap/sensors/FAN_CPLD/fan5_input",
        "thresholdMap" : {
          "4" : 25500,
          "5" : 2600
        },
        "type" : 4
      }
    },

    "SWITCH_CARD" : {
      "SC_BOARD_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "SC_BOARD_MIDDLE_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp2_input",
        "thresholdMap" : {
          "4" : 75
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "SC_BOARD_LEFT_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp3_input",
        "thresholdMap" : {
          "4" : 75
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "SC_FRONT_PANEL_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp4_input",
        "thresholdMap" : {
          "4" : 75
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "SC_TH3_DIODE1_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp7_input",
        "thresholdMap" : {
          "4" : 125
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "SC_TH3_DIODE2_TEMP" : {
        "path" : "/run/devmap/sensors/SC_BOARD_TEMP_MAX6581/temp8_input",
        "thresholdMap" : {
          "4" : 125
        },
        "compute" : "@/1000.0",
        "type" : 3
      }
    },

    "PEM" : {
      "PEM_ECB_VOUT_CH1" : {
        "path" : "/run/devmap/sensors/PEM_ECB_MAX5970/in1_input",
        "thresholdMap" : {
          "4" : 14
        },
        "compute" : "(15.5*@)/1000.0",
        "type" : 1
      },
      "PEM_ECB_VOUT_CH2" : {
        "path" : "/run/devmap/sensors/PEM_ECB_MAX5970/in2_input",
        "thresholdMap" : {
          "4" : 14
        },
        "compute" : "15.5*@/1000.0",
        "type" : 1
      },
      "PEM_ECB_IOUT_CH1" : {
        "path" : "/run/devmap/sensors/PEM_ECB_MAX5970/curr1_input",
        "thresholdMap" : {
          "4" : 60,
          "5" : 0.5
        },
        "compute" : "(48390/343)*@/1000.0",
        "type" : 2
      },
      "PEM_ECB_IOUT_CH2" : {
        "path" : "/run/devmap/sensors/PEM_ECB_MAX5970/curr2_input",
        "thresholdMap" : {
          "4" : 60,
          "5" : 0.5
        },
        "compute" : "(48390/343)*@/1000.0",
        "type" : 2
      },
      "PEM_ADC_VIN" : {
        "path" : "/run/devmap/sensors/PEM_ADC_MAX11645/in_voltage1_raw",
        "thresholdMap" : {
          "4" : 13.5,
          "5" : 10.9
        },
        "compute" : "@*2.048*7.64/4096",
        "type" : 1
      },
      "PEM_ADC_VOUT" : {
        "path" : "/run/devmap/sensors/PEM_ADC_MAX11645/in_voltage0_raw",
        "thresholdMap" : {
          "5" : 10.8
        },
        "compute" : "@*2.048*7.64/4096",
        "type" : 1
      },
      "PEM_ADC_VDROP" : {
        "path" : "/run/devmap/sensors/PEM_ADC_MAX11645/in_voltage1-voltage0_raw",
        "thresholdMap" : {
          "4" : 0.08,
          "5" : 0.0
        },
        "compute" : "@/1000.0",
        "type" : 1
      },
      "PEM_INTERNAL_TEMP" : {
        "path" : "/run/devmap/sensors/PEM_TEMP_MAX6658/temp1_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      },
      "PEM_EXTERNAL_TEMP" : {
        "path" : "/run/devmap/sensors/PEM_TEMP_MAX6658/temp2_input",
        "thresholdMap" : {
          "4" : 85
        },
        "compute" : "@/1000.0",
        "type" : 3
      }
    },

    "FanSpiner" : {
      "FS_FAN_RPM" : {
        "path" : "/run/devmap/sensors/FS_FAN_SLG4F4527/fan1_input",
        "thresholdMap" : {
          "4" : 29500,
          "5" : 2600
        },
        "type" : 4
      }
    }
  }
})";
}
} // namespace facebook::fboss::platform::sensor_service
