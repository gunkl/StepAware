# Implementation Plan: App0/App1 Partition Selector

## Overview
Add a web UI partition selector that allows users to easily switch between app0 and app1 firmware partitions for quick rollback to previous firmware versions.

## Current State
- ✅ Dual partition setup (app0: 1.5MB, app1: 1.5MB) configured in [partitions.csv](../partitions.csv)
- ✅ OTA upload functionality implemented in [ota_manager.h](../include/ota_manager.h) / [ota_manager.cpp](../src/ota_manager.cpp)
- ✅ Current partition detection via `getCurrentPartition()`
- ✅ Web API OTA endpoints: `/api/ota/upload`, `/api/ota/status`
- ✅ Firmware upload UI in dashboard (inline HTML approach)
- ✅ ESP32 partition APIs available: `esp_ota_set_boot_partition()`, `esp_ota_get_state_partition()`

## What's Missing
- Detection of backup partition state and validity
- API endpoint to list all app partitions with their info
- API endpoint to switch boot partition
- UI to display both partitions and allow rollback

## Implementation Steps

### 1. Extend OTAManager Class

**File:** [include/ota_manager.h](../include/ota_manager.h)

Add PartitionInfo struct (after Status struct around line 38):
```cpp
/**
 * @brief Partition information structure
 */
struct PartitionInfo {
    String label;              // "app0" or "app1"
    size_t size;               // Partition size in bytes
    uint32_t address;          // Flash address
    bool isRunning;            // Currently executing
    bool isBootNext;           // Will boot next
    bool isValid;              // Has valid firmware
    String version;            // Firmware version (if readable)
    String md5;                // MD5 hash (if available)
};
```

Add public methods (after getCurrentPartition() around line 115):
```cpp
/**
 * @brief Get all app partition information
 *
 * Returns details about both app0 and app1 partitions including
 * validation state, which is running, and which will boot next.
 *
 * @param partitions Array to fill (size 2)
 * @return Number of partitions found (should be 2)
 */
uint8_t getPartitionInfo(PartitionInfo partitions[2]) const;

/**
 * @brief Set boot partition for next restart
 *
 * Switches the boot partition to enable firmware rollback.
 * Validates target partition before switching.
 *
 * @param label Partition label ("app0" or "app1")
 * @return true if switch successful
 */
bool setBootPartition(const String& label);

/**
 * @brief Validate partition has bootable firmware
 *
 * @param label Partition label to validate
 * @return true if partition is valid
 */
bool validatePartition(const String& label) const;
```

**File:** [src/ota_manager.cpp](../src/ota_manager.cpp)

Implement the three new methods at the end of the file (after clearError() around line 288):

1. `getPartitionInfo()` - Use `esp_partition_find()` iterator to enumerate both app0/app1, call `esp_ota_get_state_partition()` to validate, read `esp_app_desc_t` for version info
2. `setBootPartition()` - Validate target partition exists and is valid, call `esp_ota_set_boot_partition()`, log the change
3. `validatePartition()` - Find partition, check state is not INVALID or ABORTED

Include MOCK_HARDWARE branches returning simulated data for testing.

### 2. Add Web API Endpoints

**File:** [include/web_api.h](../include/web_api.h)

Add method declarations (after handleClearCoredump() around line 298):
```cpp
/**
 * @brief GET /api/ota/partitions - Get partition information
 */
void handleGetPartitions(AsyncWebServerRequest* request);

/**
 * @brief POST /api/ota/switch - Switch boot partition
 */
void handleSwitchPartition(AsyncWebServerRequest* request, uint8_t* data,
                           size_t len, size_t index, size_t total);
```

**File:** [src/web_api.cpp](../src/web_api.cpp)

Register routes in `begin()` method (after existing /api/ota/* routes):
```cpp
m_server->on("/api/ota/partitions", HTTP_GET, [this](AsyncWebServerRequest* req) {
    this->handleGetPartitions(req);
});

m_server->on("/api/ota/switch", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        this->handleSwitchPartition(req, data, len, index, total);
    }
);

m_server->on("/api/ota/partitions", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
    this->handleOptions(req);
});

m_server->on("/api/ota/switch", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
    this->handleOptions(req);
});
```

Implement handlers (add to end of file):
- `handleGetPartitions()` - Call `m_otaManager->getPartitionInfo()`, serialize to JSON array
- `handleSwitchPartition()` - Parse JSON body for "partition" field, validate it's "app0" or "app1", call `m_otaManager->setBootPartition()`, schedule reboot with `delay(1000); ESP.restart();`

### 3. Add Partition Selector UI

**File:** [src/web_api.cpp](../src/web_api.cpp) in `buildDashboardHTML()` method

Add HTML section in the Firmware tab (after existing upload section):
```html
<div class="card" style="margin-top:24px;">
  <h2>Partition Manager</h2>
  <p style="color:#666;margin-bottom:16px;">
    Switch between firmware partitions for easy rollback to previous version.
  </p>

  <div id="partition-list" style="margin-bottom:16px;">
    <div style="text-align:center;color:#999;">Loading partition info...</div>
  </div>

  <div style="margin-top:16px;padding:12px;background:#e0e7ff;border-left:4px solid #667eea;border-radius:6px;">
    <div style="font-weight:600;color:#3730a3;margin-bottom:4px;">ℹ️ How It Works</div>
    <ul style="margin:8px 0 0 20px;color:#4338ca;font-size:0.9em;">
      <li>Each OTA update writes to the inactive partition</li>
      <li>Switch partitions to rollback to previous firmware</li>
      <li>Device will reboot automatically after switching</li>
      <li>Cannot switch to invalid/corrupted partitions</li>
    </ul>
  </div>
</div>
```

Add JavaScript functions in the script section:
```javascript
function loadPartitionInfo() {
    // Fetch /api/ota/partitions
    // Display partition cards with:
    //   - Label (APP0 / APP1)
    //   - Badges: RUNNING (green), BOOT NEXT (orange), INVALID (red)
    //   - Version, size, address, status
    //   - Switch button (disabled for invalid/current partition)
}

function switchPartition(label) {
    // Confirm dialog
    // POST /api/ota/switch with {"partition": label}
    // Show reboot message
    // Auto-reload after 30 seconds
}

// Call loadPartitionInfo() when firmware tab is visible
```

### 4. Safety Checks

**OTAManager Level:**
- ✅ Check partition exists via `esp_partition_find_first()`
- ✅ Verify partition state (not INVALID/ABORTED) via `esp_ota_get_state_partition()`
- ✅ Prevent switching to currently running partition
- ✅ Prevent switching during OTA upload (check `m_status.inProgress`)

**Web API Level:**
- ✅ Validate JSON body contains "partition" field
- ✅ Validate partition label is "app0" or "app1" only
- ✅ Return descriptive error messages

**UI Level:**
- ✅ Disable switch button for invalid partitions (show "INVALID" badge)
- ✅ Disable switch button for currently running partition
- ✅ Confirmation dialog before switching
- ✅ Display version info to help user decide

**Edge Cases:**
- Only one valid partition → UI disables switch button
- Switch during OTA upload → API returns error "OTA upload in progress"
- Corrupted partition → UI shows red "INVALID" badge, button disabled
- Network drop during reboot → UI shows 30s timer, user reconnects manually

## Files to Modify

1. **[include/ota_manager.h](../include/ota_manager.h)**
   - Add PartitionInfo struct
   - Add getPartitionInfo(), setBootPartition(), validatePartition() declarations

2. **[src/ota_manager.cpp](../src/ota_manager.cpp)**
   - Implement getPartitionInfo() using ESP32 partition iterator APIs
   - Implement setBootPartition() with validation and esp_ota_set_boot_partition() call
   - Implement validatePartition() using esp_ota_get_state_partition()

3. **[include/web_api.h](../include/web_api.h)**
   - Add handleGetPartitions() and handleSwitchPartition() declarations

4. **[src/web_api.cpp](../src/web_api.cpp)**
   - Register /api/ota/partitions (GET) and /api/ota/switch (POST) routes
   - Implement handleGetPartitions() - return JSON array of partition info
   - Implement handleSwitchPartition() - validate input, switch partition, reboot
   - Add partition selector HTML to buildDashboardHTML() in firmware tab
   - Add loadPartitionInfo() and switchPartition() JavaScript functions

## Verification

**Unit Testing (Mock Mode):**
1. Verify getPartitionInfo() returns simulated app0/app1 data
2. Test setBootPartition() validates partition labels
3. Test API endpoints return correct JSON structure

**Integration Testing (Real Hardware):**
1. Upload new firmware to app1 via existing OTA UI
2. Verify partition selector shows both app0 (running) and app1 (boot next)
3. Click "Switch to APP1" button
4. Confirm device reboots and runs new firmware
5. Verify partition selector now shows app1 (running) and app0 (boot next)
6. Click "Switch to APP0" to rollback
7. Verify device boots back to original firmware

**Edge Case Testing:**
1. Erase app1 partition → Verify UI shows "INVALID" badge, button disabled
2. Start OTA upload, attempt switch → Verify API returns error
3. Test with only factory partition valid → Verify cannot switch
4. Disconnect network during reboot → Verify reconnection works after 30s

**Manual UI Testing:**
- [ ] Partition cards display correctly with version/size/address
- [ ] Badges show correct states (RUNNING, BOOT NEXT, INVALID)
- [ ] Switch button disabled for invalid/current partitions
- [ ] Confirmation dialog appears before switching
- [ ] Success message shown after switch
- [ ] Auto-reload works after reboot

## Success Criteria

- ✅ User can view both app0 and app1 partition status in web UI
- ✅ UI shows which partition is currently running
- ✅ UI shows which partition will boot next (after OTA or manual switch)
- ✅ User can switch to backup partition with one click + confirmation
- ✅ System prevents switching to invalid/corrupted partitions
- ✅ Device automatically reboots after partition switch
- ✅ All features work in both real hardware and mock mode
- ✅ Follows existing code patterns (HAL abstraction, inline HTML, logging)

## Implementation Time Estimate

**Not provided per CLAUDE.md guidelines** - Implementation broken into manageable phases for iterative development.

---

**Document Status:** Deferred for future implementation
**Created:** 2026-01-30
**Related Files:** [ota_manager.h](../include/ota_manager.h), [ota_manager.cpp](../src/ota_manager.cpp), [web_api.h](../include/web_api.h), [web_api.cpp](../src/web_api.cpp)
