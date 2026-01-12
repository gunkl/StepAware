# StepAware Web UI

Modern, responsive web dashboard for monitoring and controlling your StepAware system.

## Features

### 📊 Real-Time Monitoring
- **System Status**: Uptime, memory usage, connection status
- **Statistics**: Motion events, mode changes
- **Warning Indicators**: Visual alerts when motion warnings active
- **Auto-Refresh**: Updates every 2 seconds

### 🎛️ Mode Control
- **Visual Mode Selector**: Three operating modes
  - **OFF**: System disabled
  - **CONTINUOUS**: Always blinking
  - **MOTION**: Blink on motion detection
- **One-Click Switching**: Instant mode changes
- **Current Mode Display**: Always visible mode badge

### ⚙️ Configuration Editor
Tabbed interface for editing all settings:

1. **Motion Tab**
   - Warning duration (1-300 seconds)
   - PIR warmup time (10-120 seconds)

2. **LED Tab**
   - Full brightness (0-255)
   - Medium brightness (0-255)
   - Dim brightness (0-255)
   - Visual slider controls

3. **Button Tab**
   - Debounce time (10-500ms)
   - Long press duration (500-10000ms)

4. **Power Tab**
   - Power saving enable/disable
   - Deep sleep timeout (minutes)

5. **WiFi Tab**
   - SSID configuration
   - Password (masked input)
   - WiFi enable/disable

### 📋 Log Viewer
- **Real-Time Logs**: Last 50 log entries
- **Color-Coded Levels**: DEBUG, INFO, WARN, ERROR
- **Timestamps**: Precise timing (HH:MM:SS.mmm)
- **Auto-Scroll**: Latest logs always visible
- **Monospace Display**: Terminal-style view

### 💾 Configuration Management
- **Save Changes**: Persist to SPIFFS
- **Factory Reset**: Restore defaults
- **Validation**: Client-side input checks
- **Error Handling**: Clear error messages

### 🔔 Toast Notifications
- **Success Messages**: Green notifications
- **Error Alerts**: Red notifications
- **Auto-Dismiss**: 3-second timeout
- **Non-Blocking**: Slide-in animations

## File Structure

```
data/
├── index.html      # Main dashboard HTML
├── style.css       # Styles and responsive layout
├── app.js          # API integration and logic
└── README.md       # This file
```

## Technologies Used

- **Pure JavaScript**: No frameworks required
- **CSS Grid/Flexbox**: Modern responsive layout
- **Fetch API**: RESTful API integration
- **CSS Variables**: Themeable design system
- **CSS Animations**: Smooth transitions

## API Integration

The dashboard uses the StepAware REST API (`/api/*`):

| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/status | GET | System status (auto-refresh) |
| /api/config | GET | Load configuration |
| /api/config | POST | Save configuration |
| /api/mode | GET | Get current mode |
| /api/mode | POST | Change mode |
| /api/logs | GET | Retrieve log entries |
| /api/reset | POST | Factory reset |
| /api/version | GET | Firmware version |

## Responsive Design

The dashboard adapts to all screen sizes:

- **Desktop** (>768px): Two-column layout
- **Tablet** (768px): Single column
- **Mobile** (<768px): Stacked cards, touch-friendly

## Color Scheme

### Light Theme
- **Primary**: Purple gradient (#667eea → #764ba2)
- **Success**: Green (#10b981)
- **Danger**: Red (#ef4444)
- **Warning**: Orange (#f59e0b)
- **Background**: Light gray (#f5f7fa)

### Dark Elements
- **Log Viewer**: Dark terminal (#1f2937)
- **Code Blocks**: Monospace font

## Browser Compatibility

Tested on:
- ✅ Chrome 90+
- ✅ Firefox 88+
- ✅ Safari 14+
- ✅ Edge 90+

### Required Features
- CSS Grid
- Flexbox
- Fetch API
- ES6 JavaScript
- CSS Variables

## Performance

- **Lightweight**: ~50KB total (uncompressed)
- **Fast Loading**: No external dependencies
- **Efficient Updates**: Diff-based rendering
- **Low Bandwidth**: JSON-only API calls

## Deployment

### Using SPIFFS (Recommended)

1. Upload files to ESP32 SPIFFS:
```bash
pio run --target uploadfs
```

2. Access dashboard:
```
http://stepaware.local/
```

### Using SD Card

1. Copy `data/` folder to SD card root
2. Configure web server to serve from SD

### Development Server

For local development:
```bash
python -m http.server 8000
```
Then edit `app.js` to point to your ESP32 IP.

## Customization

### Colors
Edit CSS variables in `style.css`:
```css
:root {
    --primary-color: #667eea;
    --secondary-color: #764ba2;
    /* ... */
}
```

### Refresh Rate
Edit `app.js`:
```javascript
// Change 2000 to desired milliseconds
refreshInterval = setInterval(refreshStatus, 2000);
```

### Log Entry Limit
Edit API call in `app.js`:
```javascript
// Modify URL to change limit
fetch(`${API_BASE}/logs?limit=100`)
```

## Accessibility

- **Keyboard Navigation**: Full keyboard support
- **ARIA Labels**: Screen reader compatible
- **High Contrast**: Readable color combinations
- **Large Touch Targets**: Mobile-friendly (44px minimum)

## Security Considerations

### Current Implementation
- No authentication required
- Open API access
- Passwords visible in config (over HTTPS recommended)

### Future Enhancements
- [ ] API key authentication
- [ ] Password protection
- [ ] HTTPS support
- [ ] Rate limiting
- [ ] CSRF protection

## Development

### Adding New Features

1. **Add UI Element** in `index.html`
2. **Style** in `style.css`
3. **Add Logic** in `app.js`
4. **Test** across browsers

### Testing

- Test all mode switches
- Verify configuration save/load
- Check responsive layout on mobile
- Validate form inputs
- Test error handling

## Troubleshooting

### Dashboard Won't Load
- Check ESP32 WiFi connection
- Verify web server is running
- Check browser console for errors
- Ensure SPIFFS uploaded correctly

### API Errors
- Check API endpoint URLs
- Verify CORS headers
- Check network tab in browser
- Review ESP32 serial output

### Configuration Won't Save
- Check form validation
- Verify values are in valid ranges
- Check browser console for errors
- Ensure SPIFFS has free space

### Logs Not Updating
- Verify refresh interval running
- Check API `/logs` endpoint
- Ensure logs exist in buffer
- Check browser console

## License

Same as StepAware project (MIT)

## Credits

Built with ❤️ for the StepAware project

---

**Last Updated**: 2026-01-11
**Version**: 1.0.0
**Compatible With**: StepAware Firmware v0.1.0+
