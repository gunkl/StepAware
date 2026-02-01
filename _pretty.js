function showTab(tab){
document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
event.target.classList.add('active');
document.getElementById(tab+'-tab').classList.add('active');
if(tab==='config')loadConfig();
if(tab==='hardware'){
loadSensors();
loadDisplays();
loadAnimations();
}
}
let currentMode=-1;
async function fetchStatus(){
try{
const res=await fetch('/api/status');
const data=await res.json();
currentMode=data.stateMachine.mode;
document.getElementById('mode-display').textContent=data.stateMachine.modeName;
const modeIcon=document.getElementById('mode-icon');
modeIcon.className='status-icon '+(data.stateMachine.mode===0?'inactive':data.stateMachine.mode===1?'active':'');
const warningEl=document.getElementById('warning-status');
const warningIcon=document.getElementById('warning-icon');
if(data.stateMachine.warningActive){
warningEl.textContent='Active';
warningIcon.className='status-icon warning';
}
else{
warningEl.textContent='Idle';
warningIcon.className='status-icon inactive';
}
document.getElementById('motion-events').textContent=data.stateMachine.motionEvents;
const uptime=Math.floor(data.uptime/1000);
const hours=Math.floor(uptime/3600);
const mins=Math.floor((uptime%3600)/60);
document.getElementById('uptime').textContent=hours+'h '+mins+'m';
const wifiIcon=document.getElementById('wifi-icon');
const wifiCompact=document.getElementById('wifi-status-compact');
if(data.wifi){
if(data.wifi.state===3){
wifiCompact.textContent=data.wifi.ssid;
wifiIcon.className='status-icon active';
}
else if(data.wifi.state===2){
wifiCompact.textContent='Connecting';
wifiIcon.className='status-icon warning';
}
else{
wifiCompact.textContent='Disconnected';
wifiIcon.className='status-icon inactive';
}
}
if(data.power){
const battPct=data.power.batteryPercent;
const battIcon=document.getElementById('battery-icon');
const battStatus=document.getElementById('battery-status');
if(!data.power.monitoringEnabled){
if(data.power.usbPower){
battIcon.className='status-icon active';
battStatus.textContent='USB Power';
}
else{
battIcon.className='status-icon inactive';
battStatus.textContent='No battery';
}
}
else if(data.power.usbPower){
battIcon.className='status-icon active';
battStatus.textContent='USB Power';
}
else if(data.power.critical){
battIcon.className='status-icon warning';
battStatus.textContent=battPct+'% CRITICAL';
}
else if(data.power.low){
battIcon.className='status-icon warning';
battStatus.textContent=battPct+'% LOW';
}
else{
battIcon.className='status-icon '+(battPct>20?'active':'inactive');
battStatus.textContent=battPct+'%';
}
}
}
if(data.wifi){
if(data.wifi.state===3){
document.getElementById('sys-wifi').innerHTML=data.wifi.ssid+' <span class="badge badge-success">Connected</span>';
document.getElementById('sys-ip').textContent=data.wifi.ipAddress;
document.getElementById('sys-rssi').textContent=data.wifi.rssi+' dBm';
}
else if(data.wifi.state===2){
document.getElementById('sys-wifi').innerHTML='<span class="badge badge-warning">Connecting...</span>';
document.getElementById('sys-ip').textContent='--';
document.getElementById('sys-rssi').textContent='--';
}
else{
document.getElementById('sys-wifi').innerHTML='<span class="badge badge-error">Disconnected</span>';
document.getElementById('sys-ip').textContent='--';
document.getElementById('sys-rssi').textContent='--';
}
}
if(data.power){
if(!data.power.monitoringEnabled){
document.getElementById('sys-battery').textContent=data.power.usbPower?'USB Power':'No battery';
document.getElementById('sys-voltage').textContent='--';
document.getElementById('sys-power-state').textContent=data.power.usbPower?'USB_POWER':'--';
}
else{
let battText;
if(data.power.usbPower){
battText='USB Power';
}
else if(data.power.critical){
battText=data.power.batteryPercent+'% CRITICAL';
}
else if(data.power.low){
battText=data.power.batteryPercent+'% LOW';
}
else{
battText=data.power.batteryPercent+'%';
}
document.getElementById('sys-battery').textContent=battText;
document.getElementById('sys-voltage').textContent=data.power.batteryVoltage.toFixed(2)+' V';
document.getElementById('sys-power-state').textContent=data.power.stateName;
}
}
for(let i=0;
i<=2;
i++){
const btn=document.getElementById('btn-'+i);
if(i===currentMode)btn.classList.add('active');
else btn.classList.remove('active');
}
}
catch(e){
console.error('Status fetch error:',e);
}
}
async function setMode(mode){
try{
const res=await fetch('/api/mode',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
mode:mode}
)}
);
if(res.ok)fetchStatus();
}
catch(e){
}
}
async function rebootDevice(){
if(!confirm('Are you sure you want to reboot the device?\n\nThe device will restart and the web interface will be unavailable for ~10 seconds.'))return;
alert('Rebooting device now...\n\nThe page will reload automatically in 15 seconds.');
setTimeout(()=>location.reload(),15000);
try{
await fetch('/api/reboot',{
method:'POST'}
);
}
catch(e){
}
}
let currentConfig={
}
;
async function loadConfig(){
try{
const res=await fetch('/api/config');
const cfg=await res.json();
currentConfig=cfg;
document.getElementById('cfg-deviceName').value=cfg.device?.name||'';
document.getElementById('cfg-defaultMode').value=cfg.device?.defaultMode||0;
document.getElementById('cfg-wifiSSID').value=cfg.wifi?.ssid||'';
if(cfg.wifi?.password&&cfg.wifi.password.length>0){
document.getElementById('cfg-wifiPassword').value='';
document.getElementById('cfg-wifiPassword').placeholder='••••••••';
}
else{
document.getElementById('cfg-wifiPassword').value='';
document.getElementById('cfg-wifiPassword').placeholder='';
}
document.getElementById('cfg-motionWarningDuration').value=Math.round((cfg.motion?.warningDuration||30000)/1000);
document.getElementById('cfg-ledBrightnessFull').value=(cfg.led?.brightnessFull!==undefined)?cfg.led.brightnessFull:255;
document.getElementById('cfg-ledBrightnessDim').value=(cfg.led?.brightnessDim!==undefined)?cfg.led.brightnessDim:50;
document.getElementById('cfg-logLevel').value=(cfg.logging?.level!==undefined)?cfg.logging.level:2;
document.getElementById('cfg-batteryMonitoring').value=cfg.power?.batteryMonitoringEnabled?1:0;
document.getElementById('cfg-powerSaving').value=cfg.power?.savingEnabled?1:0;
var bmSel=document.getElementById('cfg-batteryMonitoring');
var psSel=document.getElementById('cfg-powerSaving');
if(bmSel.value==='0'){
psSel.value='0';
psSel.disabled=true;
}
bmSel.addEventListener('change',function(){
if(this.value==='0'){
psSel.value='0';
psSel.disabled=true;
}
else{
psSel.disabled=false;
}
}
);
document.getElementById('cfg-dirSimultaneousThreshold').value=cfg.directionDetector?.simultaneousThresholdMs||150;
document.getElementById('cfg-dirConfirmationWindow').value=cfg.directionDetector?.confirmationWindowMs||5000;
document.getElementById('cfg-dirPatternTimeout').value=cfg.directionDetector?.patternTimeoutMs||10000;
}
catch(e){
console.error('Config load error:',e);
}
}
async function saveConfig(e){
e.preventDefault();
const pwdField=document.getElementById('cfg-wifiPassword');
const cfg=JSON.parse(JSON.stringify(currentConfig));
cfg.device=cfg.device||{
}
;
cfg.device.name=document.getElementById('cfg-deviceName').value;
cfg.device.defaultMode=parseInt(document.getElementById('cfg-defaultMode').value);
cfg.wifi=cfg.wifi||{
}
;
cfg.wifi.ssid=document.getElementById('cfg-wifiSSID').value;
cfg.wifi.enabled=true;
if(pwdField.value.length>0){
cfg.wifi.password=pwdField.value;
}
cfg.motion=cfg.motion||{
}
;
cfg.motion.warningDuration=parseInt(document.getElementById('cfg-motionWarningDuration').value)*1000;
cfg.led=cfg.led||{
}
;
cfg.led.brightnessFull=parseInt(document.getElementById('cfg-ledBrightnessFull').value);
cfg.led.brightnessDim=parseInt(document.getElementById('cfg-ledBrightnessDim').value);
cfg.logging=cfg.logging||{
}
;
cfg.logging.level=parseInt(document.getElementById('cfg-logLevel').value);
cfg.power=cfg.power||{
}
;
cfg.power.batteryMonitoringEnabled=parseInt(document.getElementById('cfg-batteryMonitoring').value)===1;
cfg.power.savingEnabled=parseInt(document.getElementById('cfg-powerSaving').value)===1;
cfg.directionDetector=cfg.directionDetector||{
}
;
cfg.directionDetector.simultaneousThresholdMs=parseInt(document.getElementById('cfg-dirSimultaneousThreshold').value);
cfg.directionDetector.confirmationWindowMs=parseInt(document.getElementById('cfg-dirConfirmationWindow').value);
cfg.directionDetector.patternTimeoutMs=parseInt(document.getElementById('cfg-dirPatternTimeout').value);
let jsonStr;
try{
jsonStr=JSON.stringify(cfg);
console.log('Saving config:',JSON.stringify(cfg,null,2));
}
catch(e){
console.error('JSON.stringify failed:',e);
alert('Failed to serialize config: '+e.message);
return;
}
try{
const res=await fetch('/api/config',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:jsonStr}
);
if(res.ok){
document.getElementById('save-indicator').classList.add('show');
setTimeout(()=>document.getElementById('save-indicator').classList.remove('show'),3000);
loadConfig();
}
else{
const errorText=await res.text();
console.error('Config save failed:',res.status,errorText);
alert('Failed to save configuration: '+errorText);
}
}
catch(e){
console.error('Config save error:',e);
alert('Error: '+e.message);
}
}
let availableLogs=[];
let selectedLog=null;
async function loadAvailableLogs(){
try{
const res=await fetch('/api/debug/logs');
const data=await res.json();
availableLogs=data.logs||[];
const select=document.getElementById('logSelect');
select.innerHTML='<option value="">-- Select a log file --</option>';
availableLogs.forEach((log,index)=>{
const option=document.createElement('option');
option.value=index;
const sizeKB=(log.size/1024).toFixed(1);
option.textContent=log.name+' ('+sizeKB+' KB)';
select.appendChild(option);
}
);
}
catch(err){
console.error('Failed to load logs:',err);
}
}
function onLogSelect(){
const select=document.getElementById('logSelect');
const index=parseInt(select.value);
if(isNaN(index)){
document.getElementById('logActions').style.display='none';
document.getElementById('logInfo').style.display='none';
selectedLog=null;
return;
}
selectedLog=availableLogs[index];
document.getElementById('selectedLogName').textContent=selectedLog.name;
document.getElementById('selectedLogSize').textContent=selectedLog.size.toLocaleString();
document.getElementById('selectedLogSizeKB').textContent=(selectedLog.size/1024).toFixed(1);
document.getElementById('selectedLogPath').textContent=selectedLog.path;
document.getElementById('logActions').style.display='block';
document.getElementById('logInfo').style.display='block';
}
function downloadLog(){
if(!selectedLog)return;
const url='/api/debug/logs/'+selectedLog.name;
const a=document.createElement('a');
a.href=url;
a.download='stepaware_'+selectedLog.name+'.log';
document.body.appendChild(a);
a.click();
document.body.removeChild(a);
}
async function eraseLog(){
if(!selectedLog)return;
if(!confirm('Are you sure you want to erase '+selectedLog.name+'?'))return;
try{
const res=await fetch('/api/debug/logs/'+selectedLog.name,{
method:'DELETE'}
);
if(res.ok){
alert('Log erased successfully');
loadAvailableLogs();
}
else{
const err=await res.text();
alert('Failed to erase log: '+err);
}
}
catch(err){
console.error('Error erasing log:',err);
alert('Error erasing log');
}
}
async function eraseAllLogs(){
if(!confirm('Are you sure you want to erase ALL logs? This cannot be undone!'))return;
try{
const res=await fetch('/api/debug/logs/clear',{
method:'POST'}
);
if(res.ok){
alert('All logs erased successfully');
loadAvailableLogs();
}
else{
const err=await res.text();
alert('Failed to erase logs: '+err);
}
}
catch(err){
console.error('Error erasing logs:',err);
alert('Error erasing logs');
}
}
document.addEventListener('DOMContentLoaded',()=>{
const select=document.getElementById('logSelect');
if(select)select.addEventListener('change',onLogSelect);
}
);
let sensorSlots=[null,null,null,null];
const SENSOR_TYPES={
PIR:{
name:'PIR Motion',pins:1,config:['warmup','distanceZone']}
,IR:{
name:'IR Beam-Break',pins:1,config:['debounce']}
,ULTRASONIC:{
name:'Ultrasonic (HC-SR04)',pins:2,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}
,ULTRASONIC_GROVE:{
name:'Ultrasonic (Grove)',pins:1,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}
}
;
async function loadSensors(){
try{
const cfgRes=await fetch('/api/config');
if(!cfgRes.ok)return;
const cfg=await cfgRes.json();
sensorSlots=[null,null,null,null];
if(cfg.sensors&&Array.isArray(cfg.sensors)){
cfg.sensors.forEach(s=>{
if(s.slot>=0&&s.slot<4){
sensorSlots[s.slot]=s;
}
}
);
}
const statusRes=await fetch('/api/sensors');
if(statusRes.ok){
const status=await statusRes.json();
if(status.sensors&&Array.isArray(status.sensors)){
status.sensors.forEach(s=>{
if(s.slot>=0&&s.slot<4&&sensorSlots[s.slot]){
sensorSlots[s.slot].errorRate=s.errorRate;
sensorSlots[s.slot].errorRateAvailable=s.errorRateAvailable;
}
}
);
}
}
renderSensors();
}
catch(e){
console.error('Failed to load sensors:',e);
}
}
function renderSensors(){
const container=document.getElementById('sensors-list');
container.innerHTML='';
sensorSlots.forEach((sensor,idx)=>{
if(sensor!==null){
container.appendChild(createSensorCard(sensor,idx));
}
}
);
if(sensorSlots.filter(s=>s!==null).length===0){
container.innerHTML='<p style="color:#94a3b8;
text-align:center;
padding:20px;
">No sensors configured. Click "Add Sensor" to get started.</p>';
}
}
function createSensorCard(sensor,slotIdx){
const card=document.createElement('div');
card.className='sensor-card'+(sensor.enabled?'':' disabled');
let html='';
html+='<div class="sensor-header">';
html+='<div style="display:flex;
align-items:center;
gap:10px;
">';
html+='<span class="badge badge-'+(sensor.type===0?'success':sensor.type===1?'info':'primary')+'">'+(sensor.type===0?'PIR':sensor.type===1?'IR':sensor.type===4?'GROVE':'HC-SR04')+'</span>';
html+='<span class="sensor-title">Slot '+slotIdx+': '+(sensor.name||'Unnamed Sensor')+'</span></div>';
html+='<div class="sensor-actions">';
html+='<button class="btn btn-sm btn-'+(sensor.enabled?'warning':'success')+'" onclick="toggleSensor('+slotIdx+')">'+(sensor.enabled?'Disable':'Enable')+'</button>';
html+='<button class="btn btn-sm btn-secondary" onclick="editSensor('+slotIdx+')">Edit</button>';
html+='<button class="btn btn-sm btn-danger" onclick="removeSensor('+slotIdx+')">Remove</button>';
html+='</div></div>';
html+='<div style="display:grid;
grid-template-columns:1fr 1fr;
gap:12px;
margin-top:12px;
">';
html+='<div><div style="font-weight:600;
margin-bottom:6px;
font-size:0.9em;
">Wiring Diagram</div>';
html+='<div style="line-height:1.6;
">';
if(sensor.type===0||sensor.type===1){
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor VCC → <span style="color:#dc2626;
font-weight:600;
">3.3V</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor GND → <span style="color:#000;
font-weight:600;
">GND</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor OUT → <span style="color:#2563eb;
font-weight:600;
">GPIO '+sensor.primaryPin+'</span></div>';
}
else if(sensor.type===2){
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor VCC → <span style="color:#dc2626;
font-weight:600;
">5V</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor GND → <span style="color:#000;
font-weight:600;
">GND</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor TRIG → <span style="color:#2563eb;
font-weight:600;
">GPIO '+sensor.primaryPin+'</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Sensor ECHO → <span style="color:#2563eb;
font-weight:600;
">GPIO '+sensor.secondaryPin+'</span></div>';
}
else if(sensor.type===4){
html+='<div style="color:#64748b;
font-size:0.85em;
">Grove VCC (Red) → <span style="color:#dc2626;
font-weight:600;
">3.3V/5V</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Grove GND (Black) → <span style="color:#000;
font-weight:600;
">GND</span></div>';
html+='<div style="color:#64748b;
font-size:0.85em;
">Grove SIG (Yellow) → <span style="color:#2563eb;
font-weight:600;
">GPIO '+sensor.primaryPin+'</span></div>';
}
html+='</div></div>';
html+='<div><div style="font-weight:600;
margin-bottom:6px;
font-size:0.9em;
">Configuration</div>';
html+='<div style="line-height:1.6;
">';
if(sensor.type===0){
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Warmup:</span> <span>'+(sensor.warmupMs/1000)+'s</span></div>';
const zoneStr=(sensor.distanceZone===1?'Near (0.5-4m)':sensor.distanceZone===2?'Far (3-12m)':'None');
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Distance Zone:</span> <span>'+zoneStr+'</span></div>';
}
else if(sensor.type===2||sensor.type===4){
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Type:</span> <span>'+(sensor.type===2?'HC-SR04 (4-pin)':'Grove (3-pin)')+'</span></div>';
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Max Range:</span> <span>'+(sensor.maxDetectionDistance||3000)+'mm</span></div>';
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Warn At:</span> <span>'+sensor.detectionThreshold+'mm</span></div>';
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Direction:</span> <span>'+(sensor.enableDirectionDetection?'Enabled':'Disabled')+'</span></div>';
if(sensor.enableDirectionDetection){
const dirMode=(sensor.directionTriggerMode===0?'Approaching':sensor.directionTriggerMode===1?'Receding':'Both');
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Trigger:</span> <span>'+dirMode+'</span></div>';
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Samples:</span> <span>'+sensor.sampleWindowSize+' @ '+sensor.sampleRateMs+'ms</span></div>';
const dirSensStr=(sensor.directionSensitivity===0||sensor.directionSensitivity===undefined?'Auto':''+sensor.directionSensitivity+'mm');
html+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Dir. Sensitivity:</span> <span>'+dirSensStr+'</span></div>';
}
}
html+='</div></div>';
html+='</div>';
if(sensor.type===2||sensor.type===4){
html+='<div style="margin-top:12px;
padding:12px;
background:#f8fafc;
border-radius:6px;
border:1px solid #e2e8f0;
">';
html+='<div style="font-weight:600;
margin-bottom:8px;
font-size:0.9em;
color:#1e293b;
">Hardware Info</div>';
html+='<div style="display:flex;
align-items:center;
gap:8px;
">';
html+='<span style="font-size:0.85em;
color:#64748b;
">Error Rate:</span>';
const errorRate=sensor.errorRate!==undefined?sensor.errorRate:-1;
const errorRateAvailable=sensor.errorRateAvailable!==undefined?sensor.errorRateAvailable:false;
if(errorRate<0||!errorRateAvailable){
html+='<span style="font-size:0.85em;
color:#94a3b8;
font-style:italic;
">Not available yet</span>';
}
else{
const colorClass=errorRate<5.0?'#10b981':errorRate<15.0?'#f59e0b':'#ef4444';
const statusText=errorRate<5.0?'Excellent':errorRate<15.0?'Fair':'Poor';
html+='<span style="font-size:0.85em;
font-weight:600;
color:'+colorClass+';
">'+errorRate.toFixed(1)+'%</span>';
html+='<span style="font-size:0.8em;
color:#94a3b8;
">('+statusText+')</span>';
}
html+='</div>';
html+='<div style="font-size:0.75em;
color:#64748b;
margin-top:4px;
">Based on 100 sample test. Lower is better. Error rate will be high if distances measured are greater than the sensor\'s capabilities, but this may not be a problem for functionality.</div>';
html+='</div>';
}
card.innerHTML=html;
return card;
}
function addSensor(){
const freeSlot=sensorSlots.findIndex(s=>s===null);
if(freeSlot===-1){
alert('Maximum 4 sensors allowed. Remove a sensor first.');
return;
}
const type=prompt('Select sensor type:\n0 = PIR Motion\n1 = IR Beam-Break\n2 = Ultrasonic (HC-SR04 4-pin)\n4 = Ultrasonic (Grove 3-pin)','0');
if(type===null)return;
const typeNum=parseInt(type);
if(typeNum<0||typeNum>4||typeNum===3){
alert('Invalid sensor type');
return;
}
const name=prompt('Enter sensor name:','Sensor '+(freeSlot+1));
if(!name)return;
let defaultPin='5';
if(typeNum===2)defaultPin='8';
if(typeNum===4)defaultPin='8';
const pin=parseInt(prompt('Enter primary pin (GPIO number):',defaultPin));
if(isNaN(pin)||pin<0||pin>48){
alert('Invalid pin number');
return;
}
const sensor={
type:typeNum,name:name,primaryPin:pin,enabled:true,isPrimary:freeSlot===0,warmupMs:60000,debounceMs:50,detectionThreshold:1100,maxDetectionDistance:3000,enableDirectionDetection:true,directionTriggerMode:0,directionSensitivity:0,sampleWindowSize:3,sampleRateMs:75}
;
if(typeNum===2){
const echoPin=parseInt(prompt('Enter echo pin for HC-SR04 (GPIO number):','9'));
if(isNaN(echoPin)||echoPin<0||echoPin>48){
alert('Invalid echo pin');
return;
}
sensor.secondaryPin=echoPin;
}
if(typeNum===4){
sensor.secondaryPin=0;
}
if(typeNum===0){
sensor.warmupMs=60000;
sensor.debounceMs=0;
sensor.enableDirectionDetection=false;
sensor.distanceZone=0;
}
sensorSlots[freeSlot]=sensor;
renderSensors();
saveSensors();
}
function removeSensor(slotIdx){
if(!confirm('Remove sensor from slot '+slotIdx+'?'))return;
sensorSlots[slotIdx]=null;
renderSensors();
saveSensors();
}
function toggleSensor(slotIdx){
if(sensorSlots[slotIdx]){
sensorSlots[slotIdx].enabled=!sensorSlots[slotIdx].enabled;
renderSensors();
saveSensors();
}
}
function editSensor(slotIdx){
const sensor=sensorSlots[slotIdx];
if(!sensor)return;
const newName=prompt('Sensor name:',sensor.name);
if(newName!==null&&newName.length>0){
sensor.name=newName;
}
if(sensor.type===0){
const warmup=parseInt(prompt('PIR warmup time (seconds):',sensor.warmupMs/1000));
if(!isNaN(warmup)&&warmup>=1&&warmup<=120)sensor.warmupMs=warmup*1000;
const zoneStr=prompt('Distance Zone:\n0=None (default)\n1=Near (0.5-4m, position lower)\n2=Far (3-12m, position higher)',sensor.distanceZone||0);
if(zoneStr!==null){
const zone=parseInt(zoneStr);
if(!isNaN(zone)&&zone>=0&&zone<=2)sensor.distanceZone=zone;
}
}
else if(sensor.type===2||sensor.type===4){
const maxDist=parseInt(prompt('Max detection distance (mm)\nSensor starts detecting at this range:',sensor.maxDetectionDistance||3000));
if(!isNaN(maxDist)&&maxDist>=100)sensor.maxDetectionDistance=maxDist;
const warnDist=parseInt(prompt('Warning trigger distance (mm)\nWarning activates when person is within:',sensor.detectionThreshold||1500));
if(!isNaN(warnDist)&&warnDist>=10)sensor.detectionThreshold=warnDist;
const dirStr=prompt('Enable direction detection? (yes/no):',(sensor.enableDirectionDetection?'yes':'no'));
if(dirStr!==null){
sensor.enableDirectionDetection=(dirStr.toLowerCase()==='yes'||dirStr==='1');
}
if(sensor.enableDirectionDetection){
const dirMode=prompt('Trigger on:\n0=Approaching (walking towards)\n1=Receding (walking away)\n2=Both directions',sensor.directionTriggerMode||0);
if(dirMode!==null&&!isNaN(parseInt(dirMode))){
sensor.directionTriggerMode=parseInt(dirMode);
}
const samples=parseInt(prompt('Rapid sample count (2-20):',sensor.sampleWindowSize||5));
if(!isNaN(samples)&&samples>=2&&samples<=20)sensor.sampleWindowSize=samples;
const interval=parseInt(prompt('Sample interval ms (50-1000):',sensor.sampleRateMs||200));
if(!isNaN(interval)&&interval>=50&&interval<=1000)sensor.sampleRateMs=interval;
const dirSens=parseInt(prompt('Direction sensitivity (mm):\n0=Auto (adaptive threshold)\nOr enter value (will be min: sample interval):',sensor.directionSensitivity||0));
if(!isNaN(dirSens)&&dirSens>=0)sensor.directionSensitivity=dirSens;
}
}
renderSensors();
saveSensors();
}
async function saveSensors(){
try{
const activeSensors=sensorSlots.map((s,idx)=>s?{
...s,slot:idx}
:null).filter(s=>s!==null);
const res=await fetch('/api/sensors',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify(activeSensors)}
);
if(res.ok){
const data=await res.json();
console.log('Sensors saved:',data);
}
else{
const err=await res.text();
console.error('Save failed:',err);
alert('Failed to save sensor configuration: '+err);
}
}
catch(e){
console.error('Save error:',e);
alert('Error saving sensors: '+e.message);
}
}
let displaySlots=[null,null];
async function loadDisplays(){
try{
const res=await fetch('/api/displays');
if(res.ok){
const data=await res.json();
if(data.displays&&Array.isArray(data.displays)){
data.displays.forEach(d=>{
if(d.slot>=0&&d.slot<2){
displaySlots[d.slot]=d;
}
}
);
renderDisplays();
}
}
}
catch(e){
console.error('Load displays error:',e);
}
}
function renderDisplays(){
const container=document.getElementById('displays-list');
if(!container)return;
container.innerHTML='';
let anyDisplay=false;
for(let i=0;
i<displaySlots.length;
i++){
if(displaySlots[i]){
anyDisplay=true;
const card=createDisplayCard(displaySlots[i],i);
container.appendChild(card);
}
}
if(!anyDisplay){
container.innerHTML='<p style="color:#94a3b8;
text-align:center;
padding:20px;
">No displays configured. Click "Add Display" to get started.</p>';
}
}
function createDisplayCard(display,slotIdx){
const card=document.createElement('div');
card.className='sensor-card';
card.style.cssText='border:1px solid #e2e8f0;
border-radius:8px;
padding:16px;
margin-bottom:12px;
background:#fff;
';
let content='';
content+='<div style="display:flex;
justify-content:space-between;
align-items:center;
margin-bottom:12px;
">';
content+='<div style="display:flex;
align-items:center;
gap:8px;
">';
const typeName=display.type===1?'8x8 Matrix':'LED';
const typeColor=display.type===1?'#3b82f6':'#10b981';
content+='<span style="background:'+typeColor+';
color:white;
padding:4px 8px;
border-radius:4px;
font-size:0.75em;
font-weight:600;
">'+typeName+'</span>';
content+='<span style="font-weight:600;
">Slot '+slotIdx+': '+display.name+'</span>';
content+='</div>';
content+='<div style="display:flex;
gap:8px;
">';
content+='<button class="btn btn-sm btn-'+(display.enabled?'warning':'success')+'" onclick="toggleDisplay('+slotIdx+')">'+(display.enabled?'Disable':'Enable')+'</button>';
content+='<button class="btn btn-sm btn-secondary" onclick="editDisplay('+slotIdx+')">Edit</button>';
content+='<button class="btn btn-sm btn-danger" onclick="removeDisplay('+slotIdx+')">Remove</button>';
content+='</div></div>';
content+='<div style="display:grid;
grid-template-columns:1fr 1fr;
gap:16px;
">';
content+='<div><div style="font-weight:600;
margin-bottom:6px;
font-size:0.9em;
">Wiring Diagram</div>';
content+='<div style="line-height:1.6;
">';
content+='<div style="color:#64748b;
font-size:0.85em;
">Matrix VCC → <span style="color:#dc2626;
font-weight:600;
">3.3V</span></div>';
content+='<div style="color:#64748b;
font-size:0.85em;
">Matrix GND → <span style="color:#000;
font-weight:600;
">GND</span></div>';
content+='<div style="color:#64748b;
font-size:0.85em;
">Matrix SDA → <span style="color:#2563eb;
font-weight:600;
">GPIO '+display.sdaPin+'</span></div>';
content+='<div style="color:#64748b;
font-size:0.85em;
">Matrix SCL → <span style="color:#2563eb;
font-weight:600;
">GPIO '+display.sclPin+'</span></div>';
content+='</div></div>';
content+='<div><div style="font-weight:600;
margin-bottom:6px;
font-size:0.9em;
">Configuration</div>';
content+='<div style="line-height:1.6;
">';
content+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">I2C Address:</span> <span>0x'+display.i2cAddress.toString(16).toUpperCase()+'</span></div>';
content+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Brightness:</span> <span>'+display.brightness+'/15</span></div>';
content+='<div style="font-size:0.85em;
"><span style="color:#64748b;
">Rotation:</span> <span>'+(display.rotation*90)+'°</span></div>';
content+='</div></div></div>';
if(display.type===1){
content+='<div style="margin-top:12px;
padding:12px;
background:#f8fafc;
border-radius:6px;
border:1px solid #e2e8f0;
">';
content+='<div style="font-weight:600;
margin-bottom:8px;
font-size:0.9em;
color:#1e293b;
">Hardware Info</div>';
content+='<div style="display:flex;
align-items:center;
gap:8px;
">';
content+='<span style="font-size:0.85em;
color:#64748b;
">I2C Error Rate:</span>';
const errorRate=display.errorRate!==undefined?display.errorRate:-1;
const errorRateAvailable=display.errorRateAvailable!==undefined?display.errorRateAvailable:false;
const txCount=display.transactionCount!==undefined?display.transactionCount:0;
if(errorRate<0||!errorRateAvailable){
const remaining=Math.max(0,10-txCount);
if(remaining>0){
content+='<span style="font-size:0.85em;
color:#94a3b8;
font-style:italic;
">Not available yet ('+remaining+' operations remaining)</span>';
}
else{
content+='<span style="font-size:0.85em;
color:#94a3b8;
font-style:italic;
">Not available yet</span>';
}
}
else{
const colorClass=errorRate<1.0?'#10b981':errorRate<5.0?'#f59e0b':'#ef4444';
const statusText=errorRate<1.0?'Excellent':errorRate<5.0?'Fair':'Poor';
content+='<span style="font-size:0.85em;
font-weight:600;
color:'+colorClass+';
">'+errorRate.toFixed(1)+'%</span>';
content+='<span style="font-size:0.8em;
color:#94a3b8;
">('+statusText+')</span>';
}
content+='</div>';
content+='<div style="font-size:0.75em;
color:#64748b;
margin-top:4px;
">Based on I2C transaction history. Lower is better.</div>';
content+='</div>';
}
card.innerHTML=content;
return card;
}
function addDisplay(){
let slot=-1;
for(let i=0;
i<2;
i++){
if(!displaySlots[i]){
slot=i;
break;
}
}
if(slot===-1){
alert('Maximum 2 displays reached');
return;
}
const name=prompt('Display name:','8x8 Matrix');
if(!name)return;
const newDisplay={
slot:slot,name:name,type:1,i2cAddress:0x70,sdaPin:7,sclPin:10,enabled:true,brightness:15,rotation:0,useForStatus:true}
;
displaySlots[slot]=newDisplay;
renderDisplays();
saveDisplays();
}
function removeDisplay(slotIdx){
if(!confirm('Remove this display?'))return;
displaySlots[slotIdx]=null;
renderDisplays();
saveDisplays();
}
function toggleDisplay(slotIdx){
if(displaySlots[slotIdx]){
displaySlots[slotIdx].enabled=!displaySlots[slotIdx].enabled;
renderDisplays();
saveDisplays();
}
}
function editDisplay(slotIdx){
const display=displaySlots[slotIdx];
if(!display)return;
const name=prompt('Display name:',display.name);
if(name){
display.name=name;
}
const brightness=prompt('Brightness (0-15):',display.brightness);
if(brightness){
display.brightness=parseInt(brightness)||15;
}
const rotation=prompt('Rotation (0,1,2,3):',display.rotation);
if(rotation){
display.rotation=parseInt(rotation)||0;
}
renderDisplays();
saveDisplays();
}
async function saveDisplays(){
try{
const activeDisplays=displaySlots.filter(d=>d!==null);
const res=await fetch('/api/displays',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify(activeDisplays)}
);
if(res.ok){
const data=await res.json();
console.log('Displays saved:',data);
}
else{
const err=await res.text();
console.error('Save failed:',err);
alert('Failed to save display configuration: '+err);
}
}
catch(e){
console.error('Save error:',e);
alert('Error saving displays: '+e.message);
}
}
async function loadAnimations(){
try{
const res=await fetch('/api/animations');
if(res.ok){
const data=await res.json();
renderAnimations(data.animations||[]);
updateAnimationSelect(data.animations||[]);
}
else{
console.error('Failed to load animations');
}
}
catch(e){
console.error('Load animations error:',e);
}
}
function renderAnimations(animations){
const container=document.getElementById('animations-list');
if(!container)return;
if(animations.length===0){
container.innerHTML='<p style="color:#94a3b8;
text-align:center;
padding:12px;
background:#f8fafc;
border-radius:4px;
">No custom animations loaded. Upload an animation file to get started.</p>';
return;
}
let html='<div style="display:grid;
gap:8px;
">';
animations.forEach((anim,idx)=>{
html+='<div style="display:flex;
justify-content:space-between;
align-items:center;
padding:12px;
background:#fff;
border:1px solid #e2e8f0;
border-radius:6px;
">';
html+='<div style="flex:1;
">';
html+='<div style="font-weight:600;
color:#1e293b;
">'+anim.name+'</div>';
html+='<div style="font-size:0.8em;
color:#64748b;
margin-top:2px;
">'+anim.frameCount+' frames';
if(anim.loop)html+=' • Looping';
html+='</div></div>';
html+='<div style="display:flex;
gap:6px;
">';
html+='<button class="btn btn-sm btn-primary" onclick="playCustomAnimation(\''+anim.name+'\')" title="Play animation" style="width:36px;
padding:8px;
">▶</button>';
html+='<button class="btn btn-sm btn-success" onclick="assignCustomAnimation(\''+anim.name+'\')" title="Assign to function" style="width:36px;
padding:8px;
">✓</button>';
html+='<button class="btn btn-sm btn-danger" onclick="deleteAnimation(\''+anim.name+'\')" title="Remove from memory" style="width:36px;
padding:8px;
">×</button>';
html+='</div></div>';
}
);
html+='</div>';
container.innerHTML=html;
}
function updateAnimationSelect(animations){
const select=document.getElementById('test-animation-select');
if(!select)return;
select.innerHTML='<option value="">Select animation...</option>';
animations.forEach(anim=>{
const opt=document.createElement('option');
opt.value=anim.name;
opt.textContent=anim.name+' ('+anim.frameCount+' frames)';
select.appendChild(opt);
}
);
}
async function uploadAnimation(){
const fileInput=document.getElementById('animation-file-input');
if(!fileInput||!fileInput.files||fileInput.files.length===0){
alert('Please select a file first');
return;
}
const file=fileInput.files[0];
if(!file.name.endsWith('.txt')){
alert('Please select a .txt animation file');
return;
}
const formData=new FormData();
formData.append('file',file);
try{
const res=await fetch('/api/animations/upload',{
method:'POST',body:formData}
);
if(res.ok){
const data=await res.json();
alert('Animation uploaded successfully: '+data.name);
fileInput.value='';
loadAnimations();
}
else{
const err=await res.text();
alert('Upload failed: '+err);
}
}
catch(e){
alert('Upload error: '+e.message);
}
}
async function playBuiltInAnimation(animType,duration){
try{
const res=await fetch('/api/animations/builtin',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
type:animType,duration:duration||0}
)}
);
if(res.ok){
console.log('Playing built-in animation: '+animType);
}
else{
const err=await res.text();
alert('Failed to play animation: '+err);
}
}
catch(e){
alert('Error playing animation: '+e.message);
}
}
function playTestAnimation(){
const select=document.getElementById('test-animation-select');
const duration=parseInt(document.getElementById('test-duration').value)||0;
if(!select||!select.value){
alert('Select an animation first');
return;
}
playAnimation(select.value,duration);
}
async function playAnimation(name,duration){
try{
const res=await fetch('/api/animations/play',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
name:name,duration:duration||0}
)}
);
if(res.ok){
console.log('Playing animation: '+name);
}
else{
const err=await res.text();
alert('Failed to play animation: '+err);
}
}
catch(e){
alert('Error playing animation: '+e.message);
}
}
async function stopAnimation(){
try{
const res=await fetch('/api/animations/stop',{
method:'POST'}
);
if(res.ok){
console.log('Animation stopped');
}
}
catch(e){
console.error('Error stopping animation:',e);
}
}
async function deleteAnimation(name){
if(!confirm('Remove "'+name+'" from memory?\n\nThis will free memory but you can re-upload the file anytime.'))return;
try{
const res=await fetch('/api/animations/'+encodeURIComponent(name),{
method:'DELETE'}
);
if(res.ok){
alert('Animation removed from memory');
loadAnimations();
}
else{
const err=await res.text();
alert('Failed to delete: '+err);
}
}
catch(e){
alert('Delete error: '+e.message);
}
}
function showAnimationHelp(){
alert('Animation File Format:\n\n'+'name=MyAnimation\n'+'loop=true\n'+'frame=11111111,10000001,...,100\n\n'+'• Each frame: 8 binary bytes + delay (ms)\n'+'• Max 16 frames per animation\n'+'• Max 8 animations loaded at once\n\n'+'See /data/animations/README.md for examples');
}
async function downloadTemplate(animType){
try{
console.log('Downloading template for:',animType);
const res=await fetch('/api/animations/template?type='+animType);
console.log('Fetch complete, status:',res.status);
if(res.ok){
const text=await res.text();
const blob=new Blob([text],{
type:'text/plain'}
);
const url=URL.createObjectURL(blob);
const a=document.createElement('a');
a.href=url;
a.download=animType.toLowerCase()+'_template.txt';
document.body.appendChild(a);
a.click();
document.body.removeChild(a);
URL.revokeObjectURL(url);
console.log('Download triggered');
}
else{
const err=await res.text();
console.error('Download failed:',res.status,err);
alert('Failed to download template: '+res.status);
}
}
catch(e){
console.error('Download error:',e);
alert('Download error: '+e.message);
}
}
function playSelectedBuiltIn(){
const select=document.getElementById('builtin-animation-select');
if(!select||!select.value)return;
const duration=parseInt(document.getElementById('test-duration').value)||5000;
playBuiltInAnimation(select.value,duration);
}
function downloadSelectedTemplate(){
const select=document.getElementById('builtin-animation-select');
if(!select||!select.value)return;
downloadTemplate(select.value);
}
function assignSelectedBuiltIn(){
const select=document.getElementById('builtin-animation-select');
if(!select||!select.value)return;
const functions=['motion-alert','battery-low','boot-status','wifi-connected'];
const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];
let message='Assign "'+select.selectedOptions[0].text+'" to which function?\n\n';
for(let i=0;
i<functions.length;
i++){
message+=(i+1)+'. '+functionNames[i]+'\n';
}
const choice=prompt(message,'1');
if(!choice)return;
const idx=parseInt(choice)-1;
if(idx>=0&&idx<functions.length){
assignAnimation(functions[idx],'builtin',select.value);
}
}
async function assignAnimation(functionKey,type,animName){
try{
const res=await fetch('/api/animations/assign',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
function:functionKey,type:type,animation:animName}
)}
);
if(res.ok){
updateActiveAnimations();
alert('Animation assigned successfully');
}
else{
const err=await res.text();
alert('Failed to assign animation: '+err);
}
}
catch(e){
alert('Assignment error: '+e.message);
}
}
function updateActiveAnimations(){
fetch('/api/animations/assignments').then(res=>res.json()).then(data=>{
const panel=document.getElementById('active-animations-panel');
if(panel){
panel.style.display='block';
}
if(data['motion-alert']){
const elem=document.getElementById('anim-motion-alert');
if(elem)elem.textContent=data['motion-alert'].type==='builtin'?'Built-in: '+data['motion-alert'].name:data['motion-alert'].name;
}
if(data['battery-low']){
const elem=document.getElementById('anim-battery-low');
if(elem)elem.textContent=data['battery-low'].type==='builtin'?'Built-in: '+data['battery-low'].name:data['battery-low'].name;
}
if(data['boot-status']){
const elem=document.getElementById('anim-boot-status');
if(elem)elem.textContent=data['boot-status'].type==='builtin'?'Built-in: '+data['boot-status'].name:data['boot-status'].name;
}
if(data['wifi-connected']){
const elem=document.getElementById('anim-wifi-connected');
if(elem)elem.textContent=data['wifi-connected'].type==='builtin'?'Built-in: '+data['wifi-connected'].name:data['wifi-connected'].name;
}
}
).catch(e=>console.error('Failed to load assignments:',e));
}
function playCustomAnimation(name){
const duration=parseInt(document.getElementById('test-duration').value)||5000;
playAnimation(name,duration);
}
function assignCustomAnimation(name){
const functions=['motion-alert','battery-low','boot-status','wifi-connected'];
const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];
let message='Assign "'+name+'" to which function?\n\n';
for(let i=0;
i<functions.length;
i++){
message+=(i+1)+'. '+functionNames[i]+'\n';
}
const choice=prompt(message,'1');
if(!choice)return;
const idx=parseInt(choice)-1;
if(idx>=0&&idx<functions.length){
assignAnimation(functions[idx],'custom',name);
}
}
function validateFirmware(){
const file=document.getElementById('firmware-file').files[0];
const btn=document.getElementById('upload-btn');
if(!file){
btn.disabled=true;
return;
}
if(file.size<100000){
alert('File too small - not a valid firmware');
btn.disabled=true;
return;
}
if(file.size>2000000){
alert('File too large - exceeds 2MB limit');
btn.disabled=true;
return;
}
btn.disabled=false;
}
function uploadFirmware(){
const file=document.getElementById('firmware-file').files[0];
if(!file){
alert('Please select a firmware file');
return;
}
if(!confirm('Upload firmware and reboot device?\n\nThis will restart the device.'))return;
const formData=new FormData();
formData.append('firmware',file);
const xhr=new XMLHttpRequest();
const progressDiv=document.getElementById('upload-progress');
const progressBar=document.getElementById('upload-bar');
const statusDiv=document.getElementById('upload-status');
const uploadBtn=document.getElementById('upload-btn');
progressDiv.style.display='block';
uploadBtn.disabled=true;
xhr.upload.addEventListener('progress',(e)=>{
if(e.lengthComputable){
const percent=Math.round((e.loaded/e.total)*100);
progressBar.value=percent;
statusDiv.textContent='Uploading... '+percent+'%';
}
}
);
xhr.addEventListener('load',()=>{
if(xhr.status===200){
statusDiv.textContent='Success! Device rebooting...';
statusDiv.style.color='#10b981';
setTimeout(()=>{
alert('Firmware updated. Device is rebooting.\nReconnect in 30 seconds.');
window.location.reload();
}
,3000);
}
else{
statusDiv.textContent='Failed: '+xhr.responseText;
statusDiv.style.color='#ef4444';
uploadBtn.disabled=false;
}
}
);
xhr.addEventListener('error',()=>{
statusDiv.textContent='Upload error - check connection';
statusDiv.style.color='#ef4444';
uploadBtn.disabled=false;
}
);
xhr.open('POST','/api/ota/upload');
xhr.send(formData);
}
fetch('/api/ota/status').then(r=>r.json()).then(data=>{
const versionElem=document.getElementById('current-version');
const partitionElem=document.getElementById('current-partition');
const maxSizeElem=document.getElementById('max-firmware-size');
if(versionElem)versionElem.textContent=data.currentVersion||'Unknown';
if(partitionElem)partitionElem.textContent='Partition: '+data.currentPartition;
if(maxSizeElem)maxSizeElem.textContent='Max size: '+(data.maxFirmwareSize/1024).toFixed(0)+' KB';
}
).catch(e=>console.error('Failed to load OTA status:',e));
fetchStatus();
setInterval(fetchStatus,2000);
updateActiveAnimations();
loadAvailableLogs();
