// Test program to validate rolling buffer error rate calculation
#include <stdio.h>
#include <stdint.h>

int main() {
    int16_t successCounter = 0;
    uint16_t totalSamples = 0;

    printf("=== WARMUP PHASE (samples 1-100) ===\n");
    printf("Simulating: 5 successes, 95 failures\n\n");

    // Simulate warmup: 95 failures, 5 successes scattered
    for (int i = 1; i <= 100; i++) {
        totalSamples++;
        bool success = (i % 20 == 0); // Success every 20th sample = 5 successes

        if (success) {
            successCounter++;
        }

        if (i == 10 || i == 25 || i == 50 || i == 75 || i == 100) {
            float errorRate = 100.0f - (float)successCounter;
            printf("Sample %3d: counter=%d, errorRate=%.1f%%\n", i, successCounter, errorRate);
        }
    }

    printf("\n=== WARMUP COMPLETE ===\n");
    printf("Final counter: %d\n", successCounter);
    printf("Final error rate: %.1f%%\n\n", 100.0f - (float)successCounter);

    printf("=== ROLLING MODE (samples 101-300) ===\n");
    printf("Simulating: 99%% success rate (1 failure per 100)\n\n");

    uint32_t rollingSuccess = 0;
    uint32_t rollingFailure = 0;

    // Simulate rolling: 199 successes, 1 failure
    for (int i = 101; i <= 300; i++) {
        totalSamples++;
        bool success = (i != 150); // One failure at sample 150

        if (success) {
            rollingSuccess++;
            if (successCounter < 100) {
                successCounter++;
            }
        } else {
            rollingFailure++;
            if (successCounter > 0) {
                successCounter--;
            }
        }

        // Log key milestones
        if (i == 110 || i == 125 || i == 150 || i == 200 || i == 300) {
            float actualError = ((float)rollingFailure / (float)(rollingSuccess + rollingFailure)) * 100.0f;
            float reportedError = 100.0f - (float)successCounter;
            printf("Sample %3d: counter=%d, reportedError=%.1f%%, actualError=%.2f%%\n",
                   i, successCounter, reportedError, actualError);
        }
    }

    printf("\n=== FINAL RESULTS ===\n");
    float actualError = ((float)rollingFailure / (float)(rollingSuccess + rollingFailure)) * 100.0f;
    float reportedError = 100.0f - (float)successCounter;
    printf("Total rolling samples: %u (%u success, %u failure)\n",
           rollingSuccess + rollingFailure, rollingSuccess, rollingFailure);
    printf("Actual error rate in rolling mode: %.2f%%\n", actualError);
    printf("Reported error rate (rolling buffer): %.1f%%\n", reportedError);
    printf("Counter value: %d\n", successCounter);

    if (reportedError < 5.0f) {
        printf("\n✓ SUCCESS: Error rate converged correctly!\n");
    } else {
        printf("\n✗ FAILURE: Error rate did not converge!\n");
    }

    return 0;
}
