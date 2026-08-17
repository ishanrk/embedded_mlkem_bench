#ifndef PQC_POLY_MLKEM_CONFIG_H
#define PQC_POLY_MLKEM_CONFIG_H

#define MLK_CONFIG_SERIAL_FIPS202_ONLY
#if !defined(PQC_MLKEM_PORTABLE)
#define MLK_CONFIG_USE_NATIVE_BACKEND_ARITH
#define MLK_CONFIG_ARITH_BACKEND_FILE "arith_backend.h"
#endif
#define MLK_CONFIG_NAMESPACE_PREFIX mlkem

#include "mlkem_native_config.h"

#endif
