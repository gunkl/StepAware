# StepAware - Docker Quick Start

**Test everything without ESP32 hardware using Docker!**

## 🚀 One-Command Test

```bash
# Start the mock web server
docker-compose up mock-server
```

Then open: **http://localhost:8080**

You now have a fully functional StepAware dashboard running in Docker!

---

## 📋 What You Can Do

### 1. Test the Web Dashboard
- View real-time system status
- Switch between operating modes (OFF, CONTINUOUS, MOTION)
- Edit configuration settings
- View simulated log entries
- Perform factory reset

### 2. Build the Firmware
```bash
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo
```
✅ Builds ESP32 firmware (287KB)

### 3. Run C++ Tests
```bash
docker-compose run --rm stepaware-dev pio test -e native
```
✅ Runs 16 hardware abstraction layer tests

### 4. Run Python Tests
```bash
docker-compose run --rm stepaware-dev python test/test_logic.py
```
✅ Runs all business logic tests

---

## 🎯 Complete Test Workflow

Run everything in one session:

```bash
# Terminal 1: Start mock server
docker-compose up mock-server

# Terminal 2: Build and test
docker-compose run --rm stepaware-dev bash

# Inside container:
pio run -e esp32-devkitlipo        # Build firmware
pio test -e native                  # Run C++ tests
python test/test_logic.py           # Run Python tests
exit

# When done, stop server (Terminal 1: Ctrl+C)
docker-compose down
```

---

## 📖 Common Tasks

### Start Mock Server (Background)
```bash
docker-compose up -d mock-server
```

### View Mock Server Logs
```bash
docker-compose logs -f mock-server
```

### Stop Everything
```bash
docker-compose down
```

### Rebuild After Changes
```bash
docker-compose build
```

### Test API with curl
```bash
# Get system status
curl http://localhost:8080/api/status

# Change mode
curl -X POST http://localhost:8080/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 2}'

# Get logs
curl http://localhost:8080/api/logs
```

---

## 🌐 Access from Mobile Device

The mock server is accessible on your local network:

1. Start server: `docker-compose up mock-server`
2. Find your PC's IP address:
   - Windows: `ipconfig`
   - Linux/Mac: `ip addr show`
3. Open on mobile (same WiFi): `http://<your-pc-ip>:8080`

---

## 🔧 Troubleshooting

### Port 8080 Already in Use

**Option 1**: Stop other services using port 8080

**Option 2**: Change port in `docker-compose.yml`:
```yaml
ports:
  - "9090:8080"  # Use 9090 instead
```
Then access: `http://localhost:9090`

### Container Won't Start

```bash
# Clean everything and rebuild
docker-compose down -v
docker-compose build --no-cache
docker-compose up mock-server
```

### Changes Not Showing

Files are auto-mounted, but if not seeing changes:
```bash
# Restart container
docker-compose restart mock-server

# Or stop and start fresh
docker-compose down
docker-compose up mock-server
```

---

## 📚 More Information

- **Full Docker Guide**: [DOCKER_GUIDE.md](DOCKER_GUIDE.md)
- **Mock Server Details**: [test/MOCK_SERVER.md](test/MOCK_SERVER.md)
- **REST API Documentation**: [API.md](API.md)
- **Web UI Documentation**: [data/README.md](data/README.md)

---

## ✨ Benefits

- ✅ **No ESP32 needed** - Test everything on PC
- ✅ **No Python setup** - All dependencies in Docker
- ✅ **Instant feedback** - Edit code, refresh browser
- ✅ **Full API testing** - All endpoints functional
- ✅ **Real-time updates** - Auto-refresh every 2 seconds
- ✅ **Cross-platform** - Works on Windows, Mac, Linux

---

## 🎓 Next Steps

1. ✅ **You are here**: Testing with Docker
2. 📝 Modify web UI ([data/](data/))
3. 🔧 Add features to firmware ([src/](src/))
4. 🧪 Write tests ([test/](test/))
5. 📤 Upload to real ESP32 hardware

---

**Ready to deploy to real hardware?** See [README.md](README.md) for ESP32 setup instructions.
