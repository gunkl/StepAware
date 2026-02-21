#ifndef STEPAWARE_OTA_MANAGER_H
#define STEPAWARE_OTA_MANAGER_H

#include <Arduino.h>
#include <functional>

/**
 * @brief OTA (Over-The-Air) Firmware Update Manager
 *
 * Manages firmware updates via web interface using ESP32's built-in Update library.
 * Handles chunked uploads, validates firmware binaries, and provides progress tracking.
 *
 * Features:
 * - Chunked HTTP POST handling for large firmware files
 * - MD5 checksum validation
 * - Firmware header validation (ESP32 magic byte check)
 * - Progress tracking with percentage
 * - Graceful error handling (won't brick device)
 * - Current partition detection
 *
 * Usage:
 * 1. Call begin() during initialization
 * 2. On upload start (first chunk): handleUploadStart(totalSize)
 * 3. For each chunk: handleUploadChunk(data, len)
 * 4. On completion: handleUploadComplete()
 * 5. On error: handleUploadError()
 */
class OTAManager {
public:
    /**
     * @brief OTA upload status structure
     */
    struct Status {
        bool inProgress;           ///< Upload currently in progress
        size_t bytesWritten;       ///< Bytes successfully written
        size_t totalSize;          ///< Total firmware size (bytes)
        uint8_t progressPercent;   ///< Upload progress percentage (0-100)
        char errorMessage[128];    ///< Last error message (empty if no error)
    };

    /**
     * @brief Construct a new OTA Manager
     */
    OTAManager();

    /**
     * @brief Initialize OTA manager
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Start firmware upload
     *
     * Initializes the ESP32 Update library and prepares for firmware write.
     * Validates that total size fits in OTA partition.
     *
     * @param totalSize Total firmware size in bytes
     * @return true if upload started successfully
     */
    bool handleUploadStart(size_t totalSize);

    /**
     * @brief Write firmware chunk
     *
     * Writes a chunk of firmware data to flash. On first chunk, validates
     * the ESP32 magic byte (0xE9) to ensure valid firmware format.
     *
     * @param data Firmware data chunk
     * @param len Length of chunk
     * @return true if chunk written successfully
     */
    bool handleUploadChunk(uint8_t* data, size_t len);

    /**
     * @brief Complete firmware upload
     *
     * Finalizes the upload and verifies MD5 checksum. If successful,
     * marks the new partition as bootable.
     *
     * @return true if upload completed successfully
     */
    bool handleUploadComplete();

    /**
     * @brief Handle upload error
     *
     * Aborts the current upload and cleans up. Safe to call multiple times.
     */
    void handleUploadError();

    /**
     * @brief Get current upload status
     *
     * @return Current status structure
     */
    Status getStatus() const;

    /**
     * @brief Get maximum firmware size
     *
     * Returns the size of the OTA partition (maximum uploadable firmware size).
     *
     * @return Maximum firmware size in bytes
     */
    size_t getMaxFirmwareSize() const;

    /**
     * @brief Get current partition name
     *
     * Returns the name of the currently running partition (e.g., "app0", "app1").
     *
     * @return Current partition label
     */
    String getCurrentPartition() const;

    /**
     * @brief OTA progress callback type
     * @param percent Upload progress percentage (0-100)
     */
    typedef std::function<void(uint8_t percent)> ProgressCallback;

    /**
     * @brief Register progress callback for display updates
     * @param callback Function called on each progress update
     */
    void onProgress(ProgressCallback callback) { m_progressCallback = callback; }

private:
    Status m_status;           ///< Current upload status
    bool m_firstChunk;         ///< Flag to track first chunk (for validation)
    ProgressCallback m_progressCallback;  ///< Progress display callback (Issue #30)

    /**
     * @brief Set error message
     *
     * @param message Error message to store
     */
    void setError(const char* message);

    /**
     * @brief Clear error message
     */
    void clearError();
};

#endif // STEPAWARE_OTA_MANAGER_H
