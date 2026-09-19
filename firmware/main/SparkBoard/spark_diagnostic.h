#ifndef SPARK_DIAGNOSTIC_H
#define SPARK_DIAGNOSTIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Spark_RunFullDiagnosticSuite(void);

#ifdef __cplusplus
}

class SparkDiagnostic {
public:
    static void RunFullDiagnosticSuite();
    static void TestI2cBusScan();
    static void TestExioExpander();
    static void AnalyzeI2sRawMicStream();
    static void LogSystemHealthTelemetry();
};
#endif

#endif // SPARK_DIAGNOSTIC_H
