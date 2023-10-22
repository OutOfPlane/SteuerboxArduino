#ifndef WEBPAGE_H
#define WEBPAGE_H

const char *webpageHeader1 = R"EOF(
<!DOCTYPE html><html lang="en-US">
<head><title>
)EOF";

const char *webpageHeader2 = R"EOF(
</title>
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
article { background: #f2f2f2; padding: 1.3em; }
body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; margin: 0; padding: 0; }
div { padding: 0.5em; }
h1 { margin: 0.5em 0 0 0; padding: 0.5em; }
h2 { background-color:red;}
input { width: 100%; padding: 9px 10px; margin: 8px 0; box-sizing: border-box; border-radius: 0; border: 1px solid #555555; border-radius: 10px; }
label { color: #333; display: block; font-style: italic; font-weight: bold; }
nav { background: #0066ff; color: #fff; display: block; font-size: 1.3em; padding: 1em; }
nav b { display: block; font-size: 1.5em; margin-bottom: 0.5em; } 
textarea { width: 100%; }
td {text-align: left;}
th {text-align: left;min-width:150px;}
</style>
<script>
var getJSON = function(url, callback) {
var xhr = new XMLHttpRequest();
xhr.open('GET', url, true);
xhr.responseType = 'json';
xhr.onload = function() {
var status = xhr.status;
if (status === 200) {
callback(null, xhr.response);
} else {
callback(status, xhr.response);
}
};
xhr.send();
};
</script>
<meta charset="UTF-8">
</head>
<body>
)EOF";

const char *webpageDataTable = R"EOF(
<table>
<tr>
<th>Allgemein</th>
<th>Info</th>
</tr>
<tr>
<td>Firmware</td>
<td id="fwvers">-</td>
</tr>
<tr>
<td>Server</td>
<td id="serverstate">-</td>
</tr>
</table>

<table>
<tr>
<th>WiFi</th>
<th>Status</th>
</tr>
<tr>
<td>SSID</td>
<td id="wifissid">-</td>
</tr>
<tr>
<td>Status</td>
<td id="wifistat_x">-</td>
</tr>
<tr>
<td>IP Adresse</td>
<td id="wifiip">-</td>
</tr>
</table>

<table>
<tr>
<th>Ethernet</th>
<th>Status</th>
</tr>
<tr>
<td>IP</td>
<td id="ethip">-</td>
</tr>
<tr>
<td>Status</td>
<td id="ethstate_y">-</td>
</tr>
</table>

<table>
<tr>
<th>Analog</th>
<th>Status</th>
</tr>
<tr>
<td>AIN0</td>
<td id="AIN0_mV">-</td>
</tr>
<tr>
<td>AIN1</td>
<td id="AIN1_mV">-</td>
</tr>
<tr>
<td>AIN2</td>
<td id="AIN2_mV">-</td>
</tr>
<tr>
<td>AIN3</td>
<td id="AIN3_mV">-</td>
</tr>
</table>

<table>
<tr>
<th>Ausgang</th>
<th>Status</th>
</tr>
<tr>
<td>OUT0</td>
<td id="OUT0_s">-</td>
</tr>
<tr>
<td>OUT1</td>
<td id="OUT1_s">-</td>
</tr>
</table>
)EOF";

const char *webpageTableUpdateScript = R"EOF(
<script>
function retrieveData() {
getJSON("/data", function(err, data) {
if(err != null){
console.log(err)
}else{
for (var key in data) {
if (data.hasOwnProperty(key)) {
if(document.getElementById(key) != null){
document.getElementById(key).innerHTML = data[key];
if(key.includes("_mA"))
document.getElementById(key).innerHTML = (data[key]/1000.0).toFixed(3) + " A";
if(key.includes("_mV"))
document.getElementById(key).innerHTML = (data[key]/1000.0).toFixed(3) + " V";
if(key.includes("_p"))
document.getElementById(key).innerHTML = data[key] + " %";
if(key.includes("_s"))
document.getElementById(key).innerHTML = ["AUS", "AN"][data[key]];
if(key.includes("_y"))
document.getElementById(key).innerHTML = ["Unbek.", "Verbunden", "Getrennt"][data[key]];
if(key.includes("_x"))
document.getElementById(key).innerHTML = ["Bereit", "Nicht erreichbar", "Scan abgesch.", "Verbunden", "Verb. fehlgesch.", "Verb. verloren", "Getrennt"][data[key]];
if(key.includes("_v"))
document.getElementById(key).innerHTML = ["LOW", "HIGH"][data[key]];
if(key.includes("_d"))
document.getElementById(key).innerHTML = (data[key]/10.0).toFixed(1) + " °C";
if(key.includes("_r"))
document.getElementById(key).innerHTML = (data[key]/10.0).toFixed(1) + " %rH";
}
}
}
}
});
}
var interval = setInterval(retrieveData, 1000);
</script>
)EOF";

const char *webpageFooter = R"EOF(
</body>
</html>
)EOF";

#endif