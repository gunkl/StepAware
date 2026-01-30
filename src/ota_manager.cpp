#include "ota_manager.h"
#include "logger.h"
#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>

// ESP32 firmware magic byte (first byte of valid firmware)
#define ESP32_MAGIC_BYTE 0xE9

OTAManager::OTAManager()
    : m_firstChunk(false)
{
    memset(&m_status, 0, sizeof(m_status));
}

bool OTAManager::begin() {
    LOG_INFO("OTA Manager initialized");

    #if !MOCK_HARDWARE
    // Get currently running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        LOG_INFO("Current partition: %s (offset: 0x%x)", running->label, running->address);
    } else {
        LOG_ERROR("Failed to get running partition!");
    }

    // Get next update partition to verify OTA is working
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition) {
        LOG_INFO("Next OTA partition: %s (offset: 0x%x, size: %u bytes)",
                 update_partition->label, update_partition->address, update_partition->size);
        LOG_INFO("Max firmware size: %u bytes", update_partition->size);
    } else {
        LOG_ERROR("Failed to get next OTA partition!");
        LOG_ERROR("OTA updates will not work - check partition table");
        return false;
    }

    // Check if OTA data partition is valid
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_OTA,
        NULL
    );

    if (otadata) {
        LOG_INFO("OTA data partition found: offset=0x%x, size=%u",
                 otadata->address, otadata->size);
    } else {
        LOG_WARN("OTA data partition not found - OTA may not work correctly");
    }

    // Verify boot partition configuration
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK) {
        LOG_INFO("OTA state: %d", ota_state);
        if (ota_state == ESP_OTA_IMG_UNDEFINED) {
            LOG_WARN("OTA image state is undefined - marking as valid");
            // Mark current partition as valid to avoid rollback
            err = esp_ota_mark_app_valid_cancel_rollback();
            if (err != ESP_OK) {
                LOG_ERROR("Failed to mark app as valid: %d", err);
            }
        }
    } else {
        LOG_WARN("Failed to get OTA state: %d", err);
    }

    LOG_INFO("OTA system ready");
    #else
    LOG_INFO("OTA Manager in mock mode");
    LOG_INFO("Max firmware size: %u bytes (simulated)", getMaxFirmwareSize());
    #endif

    return true;
}

bool OTAManager::handleUploadStart(size_t totalSize) {
    LOG_INFO("OTA upload starting - size: %u bytes", totalSize);

    // Clear previous status
    memset(&m_status, 0, sizeof(m_status));
    m_firstChunk = true;

    // Validate size
    size_t maxSize = getMaxFirmwareSize();
    if (totalSize == 0) {
        setError("Invalid firmware size (0 bytes)");
        LOG_ERROR("OTA: %s", m_status.errorMessage);
        return false;
    }

    if (totalSize > maxSize) {
        setError("Firmware too large for partition");
        LOG_ERROR("OTA: Firmware size %u exceeds max %u", totalSize, maxSize);
        return false;
    }

    #if !MOCK_HARDWARE
    // Verify OTA partition is available before attempting update
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        setError("OTA partition not found");
        LOG_ERROR("OTA: esp_ota_get_next_update_partition() returned NULL");
        LOG_ERROR("OTA: Check partition table configuration");
        return false;
    }
    LOG_INFO("OTA: Target partition: %s (offset: 0x%x, size: %u bytes)",
             update_partition->label, update_partition->address, update_partition->size);
    #endif

    // Initialize ESP32 Update library
    // Note: Update.begin() will erase the target partition
    LOG_INFO("OTA: Calling Update.begin(%u, U_FLASH)...", totalSize);
    LOG_INFO("OTA: Free heap before begin: %u bytes", ESP.getFreeHeap());

    // Try to begin update with error recovery
    bool beginSuccess = false;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            LOG_WARN("OTA: Retrying Update.begin() (attempt %d/2)...", attempt + 1);
            delay(100);  // Brief delay before retry
        }

        beginSuccess = Update.begin(totalSize, U_FLASH);
        if (beginSuccess) {
            break;
        }

        // Log detailed error info
        LOG_ERROR("OTA: Update.begin() attempt %d failed", attempt + 1);
        LOG_ERROR("OTA: Error string: %s", Update.errorString());
        LOG_ERROR("OTA: Error code: %d", Update.getError());
        LOG_ERROR("OTA: Free heap: %u bytes", ESP.getFreeHeap());

        // If first attempt failed, try aborting any stale update
        if (attempt == 0) {
            LOG_INFO("OTA: Aborting any stale update session...");
            Update.abort();
        }
    }

    if (!beginSuccess) {
        setError("Failed to begin OTA update after retries");
        LOG_ERROR("OTA: Update.begin() failed permanently");
        return false;
    }

    LOG_INFO("OTA: Update.begin() succeeded, ready to write firmware");

    // Note: MD5 verification can be added later if needed
    // Update.setMD5() should only be called with a valid MD5 string, not nullptr

    m_status.inProgress = true;
    m_status.totalSize = totalSize;
    m_status.bytesWritten = 0;
    m_status.progressPercent = 0;

    LOG_INFO("OTA: Upload session started successfully");
    return true;
}

bool OTAManager::handleUploadChunk(uint8_t* data, size_t len) {
    if (!m_status.inProgress) {
        setError("No upload in progress");
        LOG_ERROR("OTA: Received chunk but no upload in progress");
        return false;
    }

    // Validate first chunk - must start with ESP32 magic byte
    if (m_firstChunk) {
        if (len == 0 || data[0] != ESP32_MAGIC_BYTE) {
            setError("Invalid firmware format (bad magic byte)");
            LOG_ERROR("OTA: Invalid firmware header - expected 0x%02X, got 0x%02X",
                     ESP32_MAGIC_BYTE, len > 0 ? data[0] : 0x00);
            handleUploadError();
            return false;
        }
        m_firstChunk = false;
        LOG_INFO("OTA: Firmware header validated (magic byte: 0x%02X)", data[0]);
    }

    // Write chunk to flash
    size_t written = Update.write(data, len);
    if (written != len) {
        setError("Failed to write firmware chunk");
        LOG_ERROR("OTA: Write failed - expected %u bytes, wrote %u bytes", len, written);
        LOG_ERROR("OTA: Update error: %s", Update.errorString());
        handleUploadError();
        return false;
    }

    m_status.bytesWritten += written;
    m_status.progressPercent = (m_status.bytesWritten * 100) / m_status.totalSize;

    // Log progress at 25%, 50%, 75%
    if (m_status.progressPercent % 25 == 0 && m_status.progressPercent > 0) {
        LOG_INFO("OTA: Upload progress: %u%% (%u / %u bytes)",
                m_status.progressPercent, m_status.bytesWritten, m_status.totalSize);
    }

    return true;
}

bool OTAManager::handleUploadComplete() {
    if (!m_status.inProgress) {
        setError("No upload in progress");
        LOG_ERROR("OTA: Complete called but no upload in progress");
        return false;
    }

    LOG_INFO("OTA: Finalizing upload - %u bytes written", m_status.bytesWritten);

    // Finalize the update
    if (!Update.end(true)) {  // true = set new partition as boot partition
        setError("Failed to complete OTA update");
        LOG_ERROR("OTA: Update.end() failed - error: %s", Update.errorString());
        handleUploadError();
        return false;
    }

    // Verify the update was successful
    if (!Update.isFinished()) {
        setError("OTA update incomplete");
        LOG_ERROR("OTA: Update not finished after end() call");
        handleUploadError();
        return false;
    }

    m_status.inProgress = false;
    m_status.progressPercent = 100;
    clearError();

    LOG_INFO("OTA: Upload completed successfully!");
    LOG_INFO("OTA: New firmware ready - reboot to apply");
    LOG_INFO("OTA: MD5 hash: %s", Update.md5String().c_str());

    return true;
}

void OTAManager::handleUploadError() {
    if (m_status.inProgress) {
        LOG_WARN("OTA: Aborting upload due to error");
        Update.abort();
        m_status.inProgress = false;
    }

    // Keep error message and progress for status reporting
    LOG_ERROR("OTA: Upload failed - %s", m_status.errorMessage);
}

OTAManager::Status OTAManager::getStatus() const {
    return m_status;
}

size_t OTAManager::getMaxFirmwareSize() const {
    #if !MOCK_HARDWARE
    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    if (partition != NULL) {
        return partition->size;
    }
    #endif

    // Default fallback (typical ESP32-C3 OTA partition size)
    return 1536 * 1024;  // 1.5 MB
}

String OTAManager::getCurrentPartition() const {
    #if !MOCK_HARDWARE
    const esp_partition_t* partition = esp_ota_get_running_partition();
    if (partition != NULL) {
        return String(partition->label);
    }
    #endif

    return "unknown";
}

void OTAManager::setError(const char* message) {
    strncpy(m_status.errorMessage, message, sizeof(m_status.errorMessage) - 1);
    m_status.errorMessage[sizeof(m_status.errorMessage) - 1] = '\0';
}

void OTAManager::clearError() {
    m_status.errorMessage[0] = '\0';
}
