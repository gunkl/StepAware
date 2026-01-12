# API Tester Agent

**Purpose**: Test all REST API endpoints to ensure they work correctly.

## When to Invoke

- After modifying src/web_api.cpp
- After changing API endpoint logic
- After config schema updates
- Before releasing new API version
- When user reports API issues

## Task Description Template

```
Test all StepAware REST API endpoints:

1. Start mock web server (docker-compose up -d mock-server)
2. Wait for server to be ready (3 seconds)
3. Test each endpoint with curl:
   - GET /api/status
   - GET /api/config
   - POST /api/config (with valid data)
   - POST /api/config (with invalid data - test validation)
   - GET /api/mode
   - POST /api/mode (with each mode: 0, 1, 2)
   - POST /api/mode (with invalid mode - test error handling)
   - GET /api/logs
   - POST /api/reset
   - GET /api/version
4. Validate:
   - HTTP status codes
   - JSON response format
   - CORS headers present
   - Error messages for invalid input
5. Stop mock server (docker-compose down)

Report results for each endpoint.
```

## Expected Tools

- Bash (for curl, docker-compose)
- Read (for checking mock server code)

## Success Criteria

- All endpoints return expected status codes
- All JSON responses are valid
- CORS headers present on all responses
- Error cases handled correctly (400 for bad input)
- Response times < 100ms

## Example Invocation

```
User: "Updated the config validation in web API"

Main AI: "Let me test all API endpoints"

→ Invokes api-tester agent

Agent returns:
Testing 8 API endpoints...

✅ GET /api/status - 200 OK (12ms)
✅ GET /api/config - 200 OK (8ms)
✅ POST /api/config (valid) - 200 OK (15ms)
❌ POST /api/config (invalid) - 200 OK (should be 400)
   Issue: Invalid battery config accepted
✅ GET /api/mode - 200 OK (6ms)
✅ POST /api/mode - 200 OK (9ms)
✅ GET /api/logs - 200 OK (11ms)
✅ POST /api/reset - 200 OK (14ms)
✅ GET /api/version - 200 OK (5ms)

7/8 tests passed (87.5%)

Main AI: "Found validation issue in POST /api/config. Let me fix..."
```

## Test Cases

### GET /api/status
```bash
curl -s http://localhost:8080/api/status

Expected:
- Status: 200
- JSON with: uptime, freeHeap, mode, modeName, warningActive, motionEvents, modeChanges
- CORS headers present
```

### POST /api/config (Valid)
```bash
curl -X POST http://localhost:8080/api/config \
  -H "Content-Type: application/json" \
  -d '{"motion":{"warningDuration":20000}}'

Expected:
- Status: 200
- Returns updated full config
- Changes persisted
```

### POST /api/config (Invalid)
```bash
curl -X POST http://localhost:8080/api/config \
  -H "Content-Type: application/json" \
  -d '{"motion":{"warningDuration":-1000}}'

Expected:
- Status: 400
- Error message: "Invalid warningDuration"
```

### POST /api/mode (Invalid)
```bash
curl -X POST http://localhost:8080/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":99}'

Expected:
- Status: 400
- Error message: "Invalid mode value"
```

## Example Output Format

```markdown
## API Test Results

### Summary
- Total Endpoints: 8
- Passed: 8/8 (100%)
- Failed: 0
- Duration: 2.3s

### Detailed Results

#### GET /api/status
- Status: ✅ 200 OK
- Response Time: 12ms
- CORS Headers: ✅ Present
- JSON Valid: ✅ Yes
- Fields Present: uptime, freeHeap, mode, modeName, warningActive, motionEvents, modeChanges

#### POST /api/config (validation test)
- Status: ✅ 400 Bad Request
- Response Time: 18ms
- Error Handling: ✅ Correct
- Error Message: "warningDuration must be between 1000 and 300000"

#### GET /api/logs
- Status: ✅ 200 OK
- Response Time: 11ms
- JSON Valid: ✅ Yes
- Log Count: 15 entries
- Newest First: ✅ Yes

### CORS Validation
✅ All endpoints include CORS headers:
- Access-Control-Allow-Origin: *
- Access-Control-Allow-Methods: GET, POST, OPTIONS
- Access-Control-Allow-Headers: Content-Type

### Performance
- Average Response Time: 11ms
- Slowest Endpoint: POST /api/config (18ms)
- Fastest Endpoint: GET /api/version (5ms)

## Recommendations
✅ All endpoints working correctly
✅ Error handling robust
✅ CORS properly configured
✅ Performance excellent
```

## Error Scenarios to Test

1. **Missing Required Fields**
   ```json
   POST /api/mode
   Body: {}
   Expected: 400 "Missing 'mode' field"
   ```

2. **Invalid JSON**
   ```json
   POST /api/config
   Body: {invalid json}
   Expected: 400 "Invalid JSON"
   ```

3. **Out of Range Values**
   ```json
   POST /api/config
   Body: {"led":{"brightnessFull":300}}
   Expected: 400 "brightnessFull must be 0-255"
   ```

4. **Wrong HTTP Method**
   ```bash
   DELETE /api/config
   Expected: 405 Method Not Allowed
   ```

## Integration Test

Test a complete workflow:
```
1. GET /api/config (get current)
2. POST /api/mode {"mode":2} (change to MOTION)
3. GET /api/status (verify mode changed)
4. POST /api/config (update warning duration)
5. GET /api/config (verify config updated)
6. GET /api/logs (check for log entries)
7. POST /api/reset (factory reset)
8. GET /api/config (verify defaults restored)
```

## Notes

- Tests should complete in < 5 seconds
- Mock server must be running
- Tests are non-destructive (using mock server)
- Should test both success and failure cases
- Critical for API reliability
