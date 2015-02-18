"use strict";

// Define our global variables
var GoogleMap     = null;
var Planes        = {};
var PlanesOrdered = [];
var SelectedPlane = null;
var FollowSelected = false;

// Variables to store the max msg/sec and range/distance
var MaxMessagesPerSec = 0.0;
var MaxDistance = 0.0;

var SpecialSquawks = {
	'7500' : { cssClass: 'squawk7500', markerColor: 'rgb(255, 85, 85)', text: 'Aircraft Hijacking' },
	'7600' : { cssClass: 'squawk7600', markerColor: 'rgb(0, 255, 255)', text: 'Radio Failure' },
	'7700' : { cssClass: 'squawk7700', markerColor: 'rgb(255, 255, 0)', text: 'General Emergency' }
};

// Get current map settings
var CenterLat, CenterLon, ZoomLvl;

var Dump1090Version = "unknown version";
var RefreshInterval = 1000;

var PlaneRowTemplate = null;

var TrackedAircraft = 0;
var TrackedAircraftPositions = 0;
var TrackedHistorySize = 0;

var SitePosition = null;

var ReceiverClock = null;

var LastReceiverTimestamp = 0;
var StaleReceiverCount = 0;
var FetchPending = null;

var MessageCountHistory = [];

// Sorting variables
var sortId = '';
var sortCompare = null;
var sortExtract = null;
var sortAscending = true;

// Formatting variables
var TrackDirections = ["North","Northeast","East","Southeast","South","Southwest","West","Northwest"];

// History variables
var CurrentHistoryFetch = null;
var PositionHistoryBuffer = [];
var PositionHistorySize = 0;

var NBSP='\u00a0';
var DEGREES='\u00b0'
var UP_TRIANGLE='\u25b2'; // U+25B2 BLACK UP-POINTING TRIANGLE
var DOWN_TRIANGLE='\u25bc'; // U+25BC BLACK DOWN-POINTING TRIANGLE

function processReceiverUpdate(data) {
	// Loop through all the planes in the data packet
	var now = data.now;
	var acs = data.aircraft;

	// Detect stats reset
	if (MessageCountHistory.length > 0 && MessageCountHistory[MessageCountHistory.length-1].messages > data.messages) {
		MessageCountHistory = [{'time' : MessageCountHistory[MessageCountHistory.length-1].time,'messages' : 0}];
	}
	// Note the message count in the history
	MessageCountHistory.push({ 'time' : now, 'messages' : data.messages});
	// .. and clean up any old values
	if ((now - MessageCountHistory[0].time) > 30){
		MessageCountHistory.shift();
	}
	
	for (var j=0; j < acs.length; j++) {
		var ac = acs[j];
		var hex = ac.hex;
		var plane = null;
		
		// Do we already have this plane object in Planes?
		if (Planes[hex]) {
			plane = Planes[hex];
		} else { // If not make it.
			plane = new PlaneObject(hex);
			plane.tr = PlaneRowTemplate.cloneNode(true);
			
			if (hex[0] === '~') { // Non-ICAO address
				plane.tr.cells[0].textContent = hex.substring(1);
				$(plane.tr).css('font-style', 'italic');
			} else {
				plane.tr.cells[0].textContent = hex;
			}
			
			plane.tr.addEventListener('click', selectPlaneByHex.bind(undefined,hex,false));
			plane.tr.addEventListener('dblclick', selectPlaneByHex.bind(undefined,hex,true));
			
			Planes[hex] = plane;
			PlanesOrdered.push(plane);
		}
		
		// Call the function update
		plane.updateData(now, ac);
	}
}

function fetchData() {
	if (FetchPending !== null && FetchPending.state() == 'pending') {
		// don't double up on fetches, let the last one resolve
		return;
	}
	
	FetchPending = $.ajax({
		url: 'data/aircraft.json',
		timeout: 5000,
		cache: false,
		dataType: 'json'
	});
	
	FetchPending.done(function(data) {
		var now = data.now;
	
		processReceiverUpdate(data);
	
		// update timestamps, visibility, history track for all planes - not only those updated
		for (var i = 0; i < PlanesOrdered.length; ++i) {
			var plane = PlanesOrdered[i];
			plane.updateTick(now, LastReceiverTimestamp);
			//PlanesOrdered[i].updateTick(now, LastReceiverTimestamp);
		}
	
		refreshTableInfo();
		refreshSelected();
	
		if (ReceiverClock) {
			var rcv = new Date(now * 1000);
			ReceiverClock.render(rcv.getUTCHours(),rcv.getUTCMinutes(),rcv.getUTCSeconds());
		}
	
		// Check for stale receiver data
		if (LastReceiverTimestamp === now) {
			StaleReceiverCount++;
			if (StaleReceiverCount > 5) {
				$("#update_error_detail").text("The data from dump1090 hasn't been updated in a while. Maybe dump1090 is no longer running?");
				$("#update_error").css('display','block');
			}
		} else {
			StaleReceiverCount = 0;
			LastReceiverTimestamp = now;
			$("#update_error").css('display','none');
		}
	});
	
	FetchPending.fail(function(jqxhr, status, error) {
		$("#update_error_detail").text("AJAX call failed (" + status + (error ? (": " + error) : "") + "). Maybe dump1090 is no longer running?");
		$("#update_error").css('display','block');
	});
}

function initialize() {
	// Set page basics
	$("head title").text(PageName);
	$("#infoblock_name").text(PageName);
	
	// Check if we have saved highscores cookies
	checkCookie();
	
	PlaneRowTemplate = document.getElementById("plane_row_template");
	
	if (!ShowClocks) {
		$('#timestamps').css('display','none');
	} else {
		// Create the clocks.
		new CoolClock({
			canvasId:       "utcclock",
			skinId:         "classic",
			displayRadius:  40,
			showSecondHand: true,
			gmtOffset:      "0", // this has to be a string!
			showDigital:    false,
			logClock:       false,
			logClockRev:    false
		});
	
		ReceiverClock = new CoolClock({
			canvasId:       "receiverclock",
			skinId:         "classic",
			displayRadius:  40,
			showSecondHand: true,
			gmtOffset:      null,
			showDigital:    false,
			logClock:       false,
			logClockRev:    false
		});
	
		// disable ticking on the receiver clock, we will update it ourselves
		ReceiverClock.tick = (function(){})
	}
	
	$("#loader").removeClass("hidden");
	
	// Get receiver metadata, reconfigure using it, then continue with initialization
	$.ajax({
		url: 'data/receiver.json',
		timeout: 5000,
		cache: false,
		dataType: 'json'
	}).done(function(data) {
		if (typeof data.lat !== "undefined") {
			SiteShow = true;
			SiteLat = data.lat;
			SiteLon = data.lon;	
			DefaultCenterLat = data.lat;
			DefaultCenterLon = data.lon;
		}
		
		Dump1090Version = data.version;
		RefreshInterval = data.refresh;
		PositionHistorySize = data.history;
	}).always(function() {
		initialize_map();
		start_load_history();
	});
}

function start_load_history() {	
	if (PositionHistorySize > 0) {
		$("#loader_progress").attr('max',PositionHistorySize);
		console.log("Starting to load history (" + PositionHistorySize + " items).");
		load_history_item(0);
	} else {
		end_load_history();
	}
}

function load_history_item(i) {
	if (i >= PositionHistorySize) {
		end_load_history();
		return;
	}
	
	console.log("Loading history #" + i);
	$("#loader_progress").attr('value',i);
	
	$.ajax({
		url: 'data/history_' + i + '.json',
		timeout: 5000,
		cache: false,
		dataType: 'json'
	}).done(function(data) {
		PositionHistoryBuffer.push(data);
		load_history_item(i+1);
	}).fail(function(jqxhr, status, error) {
		// No more history
		end_load_history();
	});
}

function end_load_history() {
	$("#loader").addClass("hidden");	
	console.log("Done loading history");
	
	if (PositionHistoryBuffer.length > 0) {
		var now, last=0;
	
		// Sort history by timestamp
		console.log("Sorting history");
		PositionHistoryBuffer.sort(function(x,y) { return (x.now - y.now); });
	
		// Process history
		for (var h = 0; h < PositionHistoryBuffer.length; ++h) {
			now = PositionHistoryBuffer[h].now;
			console.log("Applying history " + h + "/" + PositionHistoryBuffer.length + " at: " + now);
			processReceiverUpdate(PositionHistoryBuffer[h]);
	
			// update track
			console.log("Updating tracks at: " + now);
			for (var i = 0; i < PlanesOrdered.length; ++i) {
				var plane = PlanesOrdered[i];
				plane.updateTrack((now - last) + 1);
			}
	
			last = now;
		}
	
		// Final pass to update all planes to their latest state
		console.log("Final history cleanup pass");
		for (var i = 0; i < PlanesOrdered.length; ++i) {
			var plane = PlanesOrdered[i];
			plane.updateTick(now);
		}
	
		LastReceiverTimestamp = last;
	}
	
	PositionHistoryBuffer = null;
	console.log("Completing init");
	
	refreshTableInfo();
	refreshSelected();
	reaper();
	
	// Setup our timer to poll from the server.
	window.setInterval(fetchData, RefreshInterval);
	window.setInterval(reaper, 60000);
	
	// And kick off one refresh immediately.
	fetchData();
}

// Initalizes the map and starts up our timers to call various functions
function initialize_map() {
	// Load stored map settings if present
	CenterLat = Number(localStorage['CenterLat']) || DefaultCenterLat;
	CenterLon = Number(localStorage['CenterLon']) || DefaultCenterLon;
	ZoomLvl = Number(localStorage['ZoomLvl']) || DefaultZoomLvl;
	
	// Set SitePosition, initialize sorting
	if (SiteShow && (typeof SiteLat !==  'undefined') && (typeof SiteLon !==  'undefined')) {
		SitePosition = new google.maps.LatLng(SiteLat, SiteLon);
		sortByDistance();
	} else {
		SitePosition = null;
		PlaneRowTemplate.cells[5].style.display = 'none'; // hide distance column
		document.getElementById("distance").style.display = 'none'; // hide distance header
		sortByAltitude();
	}
	
	// Make a list of all the available map IDs
	var mapTypeIds = [];
	for(var type in google.maps.MapTypeId) {
		mapTypeIds.push(google.maps.MapTypeId[type]);
	}
	// Push OSM on to the end
	mapTypeIds.push("OSM");
	mapTypeIds.push("dark_map");
	
	// Styled Map to outline airports and highways
	var styles = [
		{
			"featureType": "administrative",
			"stylers": [{ "visibility": "off" }]
		},{
			"featureType": "landscape",
			"stylers": [{ "visibility": "off" }]
		},{
			"featureType": "poi",
			"stylers": [{ "visibility": "off" }]
		},{
			"featureType": "road",
			"stylers": [{ "visibility": "off" }]
		},{
			"featureType": "transit",
			"stylers": [{ "visibility": "off" }]
		},{
			"featureType": "landscape",
			"stylers": [
				{ "visibility": "on" },
				{ "weight": 8 },
				{ "color": "#000000" }
			]
		},{
			"featureType": "water",
			"stylers": [{ "lightness": -74 }]
		},{
			"featureType": "transit.station.airport",
			"stylers": [
				{ "visibility": "on" },
				{ "weight": 8 },
				{ "invert_lightness": true },
				{ "lightness": 27 }
			]
		},{
			"featureType": "road.highway",
			"stylers": [
				{ "visibility": "simplified" },
				{ "invert_lightness": true },
				{ "gamma": 0.3 }
			]
		},{
			"featureType": "road",
			"elementType": "labels",
			"stylers": [{ "visibility": "off" }]
		}
	];
	
	// Add our styled map
	var styledMap = new google.maps.StyledMapType(styles, {name: "Dark Map"});
	
	// Line 398: mapTypeId: google.maps.MapTypeId.ROADMAP,
	
	var mapOptions;
	
	if(AutoLoadTerrainMap){
		mapOptions = {
			center: new google.maps.LatLng(CenterLat, CenterLon),
			zoom: ZoomLvl,
			mapTypeId: google.maps.MapTypeId.TERRAIN,
			mapTypeControl: true,
			streetViewControl: false,
			mapTypeControlOptions: {
				mapTypeIds: mapTypeIds,
				position: google.maps.ControlPosition.TOP_LEFT,
				style: google.maps.MapTypeControlStyle.DROPDOWN_MENU
			}
		};
	}
	else{
		mapOptions = {
			center: new google.maps.LatLng(CenterLat, CenterLon),
			zoom: ZoomLvl,
			mapTypeId: google.maps.MapTypeId.ROADMAP,
			mapTypeControl: true,
			streetViewControl: false,
			mapTypeControlOptions: {
				mapTypeIds: mapTypeIds,
				position: google.maps.ControlPosition.TOP_LEFT,
				style: google.maps.MapTypeControlStyle.DROPDOWN_MENU
			}
		};	
	}
	
	// Define the Google Map
	
	GoogleMap = new google.maps.Map(document.getElementById("map_canvas"), mapOptions);
	
	//Define OSM map type pointing at the OpenStreetMap tile server
	GoogleMap.mapTypes.set("OSM", new google.maps.ImageMapType({
		getTileUrl: function(coord, zoom) {return "http://tile.openstreetmap.org/" + zoom + "/" + coord.x + "/" + coord.y + ".png";},
		tileSize: new google.maps.Size(256, 256),
		name: "OpenStreetMap",
		maxZoom: 18
	}));
	
	GoogleMap.mapTypes.set("dark_map", styledMap);
	
	// Listeners for newly created Map
	google.maps.event.addListener(GoogleMap, 'center_changed', function() {
		localStorage['CenterLat'] = GoogleMap.getCenter().lat();
		localStorage['CenterLon'] = GoogleMap.getCenter().lng();
		
		if (FollowSelected) {
			// On manual navigation, disable follow
			var selected = Planes[SelectedPlane];
			
			if (Math.abs(GoogleMap.getCenter().lat() - selected.position.lat()) > 0.0001 && Math.abs(GoogleMap.getCenter().lng() - selected.position.lng()) > 0.0001) {
				FollowSelected = false;
				refreshSelected();
			}
		}
	});
	
	google.maps.event.addListener(GoogleMap, 'zoom_changed', function() {
		localStorage['ZoomLvl']  = GoogleMap.getZoom();
	});
	
	// Add home marker if requested
	if (SitePosition) {
		var markerImage = new google.maps.MarkerImage(
			'http://maps.google.com/mapfiles/kml/pal4/icon57.png',
			new google.maps.Size(32, 32),   // Image size
			new google.maps.Point(0, 0),    // Origin point of image
			new google.maps.Point(16, 16)   // Position where marker should point
		);
		
		var marker = new google.maps.Marker({
			position: SitePosition,
			map: GoogleMap,
			icon: markerImage,
			title: SiteName,
			zIndex: -99999
		});
	
		if (SiteCircles) {
			for (var i=0;i<SiteCirclesDistances.length;i++) {
				drawCircle(marker, SiteCirclesDistances[i]); // in meters
			}
		}
	}
}

// This looks for planes to reap out of the master Planes variable
function reaper() {
	//console.log("Reaping started..");
	// Look for planes where we have seen no messages for >300 seconds
	var newPlanes = [];
	
	for (var i = 0; i < PlanesOrdered.length; ++i) {
		var plane = PlanesOrdered[i];
		if (plane.seen > 300) { // Reap it.
			//console.log("Reaping " + plane.icao);
			//console.log("parent " + plane.tr.parentNode);
			plane.tr.parentNode.removeChild(plane.tr);
			plane.tr = null;
			
			delete Planes[plane.icao];
			
			plane.destroy();
		} else {
			// Keep it.
			newPlanes.push(plane);
		}
	};
	
	PlanesOrdered = newPlanes;
	refreshTableInfo();
	refreshSelected();
}

// Refresh the detail window about the plane
function refreshSelected() {
	var selected = false;
	if (typeof SelectedPlane !== 'undefined' && SelectedPlane != "ICAO" && SelectedPlane != null) {
		selected = Planes[SelectedPlane];
	}
	
	if (!selected) {
		$('#selected_infoblock').css('display','none');
		$('#dump1090_infoblock').css('display','block');
		$('#dump1090_version').text(Dump1090Version);
		$('#dump1090_total_ac').text(TrackedAircraft);
		$('#dump1090_total_ac_positions').text(TrackedAircraftPositions);
		$('#dump1090_total_history').text(TrackedHistorySize);
		
	
		var message_rate = null;
		
		if (MessageCountHistory.length > 1) {
			var message_time_delta = MessageCountHistory[MessageCountHistory.length-1].time - MessageCountHistory[0].time;
			var message_count_delta = MessageCountHistory[MessageCountHistory.length-1].messages - MessageCountHistory[0].messages;
			
			if (message_time_delta > 0) {
				message_rate = message_count_delta / message_time_delta;
			}
		}
	
		if (message_rate !== null){
			$('#dump1090_message_rate').text(message_rate.toFixed(1));
			
			if(message_rate > MaxMessagesPerSec){
				MaxMessagesPerSec = message_rate;
				
				if(SaveHighscores) {
					setCookie("msgrate",MaxMessagesPerSec,SaveLastsForDays);
				}
			}
		} else {
			$('#dump1090_message_rate').text("n/a");
		}
	
		$('#dump1090_max_distance').text(format_distance_long(MaxDistance));
		$('#dump1090_max_message_rate').text((MaxMessagesPerSec * 1.0).toFixed(1));
		
		return;
	}
	
	$('#dump1090_infoblock').css('display','none');
	$('#selected_infoblock').css('display','block');
	
	if (selected.flight !== null && selected.flight !== "") {
	$('#selected_callsign').text(selected.flight);
	$('#selected_links').css('display','inline');
	$('#selected_fr24_link').attr('href','http://fr24.com/'+selected.flight);
	$('#selected_flightstats_link').attr('href','http://www.flightstats.com/go/FlightStatus/flightStatusByFlight.do?flightNumber='+selected.flight);
	$('#selected_flightaware_link').attr('href','http://flightaware.com/live/flight/'+selected.flight);
	} else {
	$('#selected_callsign').text('n/a');
	$('#selected_links').css('display','none');
	}
	
	var emerg = document.getElementById('selected_emergency');
	if (selected.squawk in SpecialSquawks) {
	emerg.className = SpecialSquawks[selected.squawk].cssClass;
	emerg.textContent = NBSP + 'Squawking: ' + SpecialSquawks[selected.squawk].text + NBSP ;
	} else {
	emerg.className = 'hidden';
	}
	
	$("#selected_altitude").text(format_altitude_long(selected.altitude, selected.vert_rate));
	
	if (selected.squawk === null || selected.squawk === '0000') {
	$('#selected_squawk').text('n/a');
	} else {
	$('#selected_squawk').text(selected.squawk);
	}
	
	$('#selected_speed').text(format_speed_long(selected.speed));
	$('#selected_icao').text(selected.icao.toUpperCase());
	$('#airframes_post_icao').attr('value',selected.icao);
	$('#selected_track').text(format_track_long(selected.track));
	
	if (selected.seen <= 1) {
	$('#selected_seen').text('now');
	} else {
	$('#selected_seen').text(selected.seen.toFixed(1) + 's');
	}
	
	if (selected.position === null) {
	$('#selected_position').text('n/a');
	$('#selected_follow').addClass('hidden');
	} else {
	if (selected.seen_pos > 1) {
	$('#selected_position').text(format_latlng(selected.position) + " (" + selected.seen_pos.toFixed(1) + "s)");
	} else {
	$('#selected_position').text(format_latlng(selected.position));
	}
	$('#selected_follow').removeClass('hidden');
	if (FollowSelected) {
	$('#selected_follow').css('font-weight', 'bold');
	GoogleMap.panTo(selected.position);
	} else {
	$('#selected_follow').css('font-weight', 'normal');
	}
	}
	
	$('#selected_sitedist').text(format_distance_long(selected.sitedist));
	$('#selected_rssi').text(selected.rssi.toFixed(1) + ' dBFS');
}

// Refreshes the larger table of all the planes
function refreshTableInfo() {
	var show_squawk_warning = false;
	
	TrackedAircraft = 0
	TrackedAircraftPositions = 0
	TrackedHistorySize = 0
	
	for (var i = 0; i < PlanesOrdered.length; ++i) {
		var tableplane = PlanesOrdered[i];
		TrackedHistorySize += tableplane.history_size;
		
		if (!tableplane.visible) {
			tableplane.tr.className = "plane_table_row hidden";
		} else {
			if(tableplane.sitedist > MaxDistance) {
				MaxDistance = tableplane.sitedist;
				
				if(SaveHighscores) {
					setCookie("range",MaxDistance,SaveLastsForDays);
				}
			}
			
			TrackedAircraft++;
			var classes = "plane_table_row";
	
			if (tableplane.position !== null){
				classes += " vPosition";
			}
			
			if (tableplane.icao == SelectedPlane){
				classes += " selected";
			}
	
			if (tableplane.squawk in SpecialSquawks) {
				classes = classes + " " + SpecialSquawks[tableplane.squawk].cssClass;
				show_squawk_warning = true;
			}
	
			// ICAO doesn't change
			tableplane.tr.cells[1].textContent = (tableplane.flight !== null ? tableplane.flight : "");
			tableplane.tr.cells[2].textContent = (tableplane.squawk !== null ? tableplane.squawk : "");
			tableplane.tr.cells[3].textContent = format_altitude_brief(tableplane.altitude, tableplane.vert_rate);
			tableplane.tr.cells[4].textContent = format_speed_brief(tableplane.speed);
	
			if (tableplane.position !== null){
				++TrackedAircraftPositions;
			}
	
			tableplane.tr.cells[5].textContent = format_distance_brief(tableplane.sitedist);
			tableplane.tr.cells[6].textContent = format_track_brief(tableplane.track);
			tableplane.tr.cells[7].textContent = tableplane.messages;
			tableplane.tr.cells[8].textContent = tableplane.seen.toFixed(0);
	
			tableplane.tr.className = classes;
		}
	}
	
	if (show_squawk_warning) {
		$("#SpecialSquawkWarning").css('display','block');
	} else {
		$("#SpecialSquawkWarning").css('display','none');
	}
	
	resortTable();
}

//
// ---- Highscore sace/load functions through cookies --- based on http://www.w3schools.com/js/js_cookies.asp
// Add some checking algorithm that will monitor date changes, create history cookie, update history_size cookie, and create new current highscore cookie?
// also keep some all-time cookie?
//
function setCookie(cname, cvalue, exdays) {
    var d = new Date();
    d.setTime(d.getTime() + (exdays*24*60*60*1000));
    var expires = "expires="+d.toUTCString();
    document.cookie = cname + "=" + cvalue + "; " + expires;
}

function getCookie(cname) {
    var name = cname + "=";
    var ca = document.cookie.split(';');
    for(var i=0; i<ca.length; i++) {
        var c = ca[i];
        while (c.charAt(0) == ' ') {
            c = c.substring(1);
		}
         if (c.indexOf(name) == 0) {
            return c.substring(name.length,c.length);
		 }
    }
    return "";
}

function checkCookie() {
    MaxDistance = getCookie("range");
    MaxMessagesPerSec = getCookie("msgrate");
    console.log("found highscore cookies -> range = " + MaxDistance + " and msg_rate = " + MaxMessagesPerSec);
    
    if(MaxDistance > 0.0) {
        $('#dump1090_max_distance').text(format_distance_long(MaxDistance));
    }
    
    if(MaxMessagesPerSec > 0.0) {
        $('#dump1090_max_message_rate').text((MaxMessagesPerSec * 1.0).toFixed(1));
    }
}

//
// ---- table sorting ----
//

function compareAlpha(xa,ya) {
	if (xa === ya) {
		return 0;
	} else if (xa < ya) {
		return -1;
	} else {
		return 1;
	}
}

function compareNumeric(xf,yf) {
	if (Math.abs(xf - yf) < 1e-9) {
		return 0;
	} else {
		return xf - yf;
	}
}

function sortByICAO()     { sortBy('icao',    compareAlpha,   function(x) { return x.icao; }); }
function sortByFlight()   { sortBy('flight',  compareAlpha,   function(x) { return x.flight; }); }
function sortBySquawk()   { sortBy('squawk',  compareAlpha,   function(x) { return x.squawk; }); }
function sortByAltitude() { sortBy('altitude',compareNumeric, function(x) { return (x.altitude == "ground" ? -1e9 : x.altitude); }); }
function sortBySpeed()    { sortBy('speed',   compareNumeric, function(x) { return x.speed; }); }
function sortByDistance() { sortBy('sitedist',compareNumeric, function(x) { return x.sitedist; }); }
function sortByTrack()    { sortBy('track',   compareNumeric, function(x) { return x.track; }); }
function sortByMsgs()     { sortBy('msgs',    compareNumeric, function(x) { return x.messages; }); }
function sortBySeen()     { sortBy('seen',    compareNumeric, function(x) { return x.seen; }); }

function sortFunction(x,y) {
	var xv = x._sort_value;
	var yv = y._sort_value;
	
	// always sort missing values at the end, regardless of
	// ascending/descending sort
	if (xv == null && yv == null) {
		return x._sort_pos - y._sort_pos;
	} else if (xv == null) {
		return 1;
	} else if (yv == null) {
		return -1;
	}
	
	var c = sortAscending ? sortCompare(xv,yv) : sortCompare(yv,xv);
	if (c !== 0) {
		return c;
	}
	
	return x._sort_pos - y._sort_pos;
}

function resortTable() {
	// number the existing rows so we can do a stable sort
	// regardless of whether sort() is stable or not.
	// Also extract the sort comparison value.
	for (var i = 0; i < PlanesOrdered.length; ++i) {
		PlanesOrdered[i]._sort_pos = i;
		PlanesOrdered[i]._sort_value = sortExtract(PlanesOrdered[i]);
	}
	
	PlanesOrdered.sort(sortFunction);
	
	var tbody = document.getElementById('tableinfo').tBodies[0];
	for (var i = 0; i < PlanesOrdered.length; ++i) {
		tbody.appendChild(PlanesOrdered[i].tr);
	}
}

function sortBy(id,sc,se) {
	if (id === sortId) {
		sortAscending = !sortAscending;
		PlanesOrdered.reverse(); // this correctly flips the order of rows that compare equal
	} else {
		sortAscending = true;
	}
	
	sortId = id;
	sortCompare = sc;
	sortExtract = se;
	
	resortTable();
}

function selectPlaneByHex(hex,autofollow) {
	//console.log("select: " + hex);
	// If SelectedPlane has something in it, clear out the selected
	if (SelectedPlane != null) {
		Planes[SelectedPlane].selected = false;
		Planes[SelectedPlane].clearLines();
		Planes[SelectedPlane].updateMarker();
		$(Planes[SelectedPlane].tr).removeClass("selected");
	}
	
	// If we are clicking the same plane, we are deselected it.
	if (SelectedPlane === hex) {
		hex = null;
	}
	
	if (hex !== null) { // Assign the new selected
		SelectedPlane = hex;
		Planes[SelectedPlane].selected = true;
		Planes[SelectedPlane].updateLines();
		Planes[SelectedPlane].updateMarker();
		$(Planes[SelectedPlane].tr).addClass("selected");
	} else {
		SelectedPlane = null;
	}
	
	if (SelectedPlane !== null && autofollow) {
		FollowSelected = true;
		if (GoogleMap.getZoom() < 8) {
			GoogleMap.setZoom(8);
		}
	} else {
		FollowSelected = false;
	}
	
	refreshSelected();
}

function toggleFollowSelected() {
	FollowSelected = !FollowSelected;
	if (FollowSelected && GoogleMap.getZoom() < 8) {
		GoogleMap.setZoom(8);
	}
	refreshSelected();
}

function resetMax() {
	MaxDistance = 0.0;
	MaxMessagesPerSec = 0.0;
	
	if(SaveHighscores) {
		setCookie("range",0.0,SaveLastsForDays);
		setCookie("msgrate",0.0,SaveLastsForDays);
	}
}

function resetMap() {
	// Reset localStorage values and map settings
	localStorage['CenterLat'] = CenterLat = DefaultCenterLat;
	localStorage['CenterLon'] = CenterLon = DefaultCenterLon;
	localStorage['ZoomLvl']   = ZoomLvl = DefaultZoomLvl;
	
	// Set and refresh
	GoogleMap.setZoom(ZoomLvl);
	GoogleMap.setCenter(new google.maps.LatLng(CenterLat, CenterLon));
	
	//deselect any previously selected plane
	selectPlaneByHex(null,false);
}

function drawCircle(marker, distance) {
	if (typeof distance === 'undefined') {
		return false;
	
		// code never reaches this part??
	
		if (!(!isNaN(parseFloat(distance)) && isFinite(distance)) || distance < 0) {
			return false;
		}
	}
	distance *= 1000.0;
	
	if (!EnableMetric) {
		distance *= 1.852;
	}
	
	// Add circle overlay and bind to marker
	var circle = new google.maps.Circle({
		map: GoogleMap,
		radius: distance, // In meters
		fillOpacity: 0.0,
		strokeWeight: 1,
		strokeOpacity: 0.3
	});
	
	circle.bindTo('center', marker, 'position');
}
