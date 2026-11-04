#pragma once

#include <memory>

#include "comm/protocol/ems/EmsBoschProtocol.h"
#include "comm/protocol/generic/GenericProtocol.h"
#include "comm/protocol/ivt/IvtRego1000Protocol.h"
#include "comm/protocol/ivt/IvtRego5200Protocol.h"
#include "comm/protocol/ivt/IvtRego600Protocol.h"
#include "comm/protocol/ivt/IvtRego800Protocol.h"
#include "comm/protocol/nibe/NibeEB100Protocol.h"
#include "comm/protocol/nibe/NibeStyr2002Protocol.h"
#include "comm/protocol/thermia/ThermiaProtocol.h"
#include "heatpump/HeatPumpDetector.h"

/// Registers every protocol shipped with the library.
inline void registerAllProtocols(HeatPumpDetector& detector) {
    detector.registerProtocol(
        HeatPumpProtocol::IVT_REGO800,
        [] { return std::make_unique<IvtRego800Protocol>(); },
        "IVT Rego 800");

    detector.registerProtocol(
        HeatPumpProtocol::IVT_REGO600,
        [] { return std::make_unique<IvtRego600Protocol>(); },
        "IVT Rego 600 / 2-3000");

    detector.registerProtocol(
        HeatPumpProtocol::IVT_REGO1000,
        [] { return std::make_unique<IvtRego1000Protocol>(); },
        "IVT Rego 1000 / Pro Control 500");

    detector.registerProtocol(
        HeatPumpProtocol::IVT_REGO5200,
        [] { return std::make_unique<IvtRego5200Protocol>(); },
        "IVT Rego 5200 / Regin");

    detector.registerProtocol(
        HeatPumpProtocol::NIBE_EB100,
        [] { return std::make_unique<NibeEB100Protocol>(); },
        "NIBE EB100");

    detector.registerProtocol(
        HeatPumpProtocol::NIBE_STYR2002,
        [] { return std::make_unique<NibeStyr2002Protocol>(); },
        "NIBE Styr2002");

    detector.registerProtocol(
        HeatPumpProtocol::BOSCH_EMS_PROCONTROL,
        [] { return std::make_unique<EmsBoschProtocol>(); },
        "Bosch EMS / ProControl");

    detector.registerProtocol(
        HeatPumpProtocol::THERMIA_DIPLOMAT,
        [] { return std::make_unique<ThermiaProtocol>(); },
        "Thermia Diplomat / Villa Classic / Genesis");

    detector.registerProtocol(
        HeatPumpProtocol::GENERIC,
        [] { return std::make_unique<GenericProtocol>(); },
        "Generic probe");
}
