"use strict";

var PlaneSvg = "M 0,0 " +
	"M 1.9565564,41.694305 C 1.7174505,40.497708 1.6419973,38.448747 " +
	"1.8096508,37.70494 1.8936398,37.332056 2.0796653,36.88191 2.222907,36.70461 " +
	"2.4497603,36.423844 4.087816,35.47248 14.917931,29.331528 l 12.434577," +
	"-7.050718 -0.04295,-7.613412 c -0.03657,-6.4844888 -0.01164,-7.7625804 " +
	"0.168134,-8.6194061 0.276129,-1.3160905 0.762276,-2.5869575 1.347875," +
	"-3.5235502 l 0.472298,-0.7553719 1.083746,-0.6085497 c 1.194146,-0.67053522 " +
	"1.399524,-0.71738842 2.146113,-0.48960552 1.077005,0.3285939 2.06344," +
	"1.41299352 2.797602,3.07543322 0.462378,1.0469993 0.978731,2.7738408 " +
	"1.047635,3.5036272 0.02421,0.2570284 0.06357,3.78334 0.08732,7.836246 0.02375," +
	"4.052905 0.0658,7.409251 0.09345,7.458546 0.02764,0.04929 5.600384,3.561772 " +
	"12.38386,7.805502 l 12.333598,7.715871 0.537584,0.959688 c 0.626485,1.118378 " +
	"0.651686,1.311286 0.459287,3.516442 -0.175469,2.011604 -0.608966,2.863924 " +
	"-1.590344,3.127136 -0.748529,0.200763 -1.293144,0.03637 -10.184829,-3.07436 " +
	"C 48.007733,41.72562 44.793806,40.60197 43.35084,40.098045 l -2.623567," +
	"-0.916227 -1.981212,-0.06614 c -1.089663,-0.03638 -1.985079,-0.05089 -1.989804," +
	"-0.03225 -0.0052,0.01863 -0.02396,2.421278 -0.04267,5.339183 -0.0395,6.147742 " +
	"-0.143635,7.215456 -0.862956,8.845475 l -0.300457,0.680872 2.91906,1.361455 " +
	"c 2.929379,1.366269 3.714195,1.835385 4.04589,2.41841 0.368292,0.647353 " +
	"0.594634,2.901439 0.395779,3.941627 -0.0705,0.368571 -0.106308,0.404853 " +
	"-0.765159,0.773916 L 41.4545,62.83158 39.259237,62.80426 c -6.030106,-0.07507 " +
	"-16.19508,-0.495041 -16.870991,-0.697033 -0.359409,-0.107405 -0.523792," +
	"-0.227482 -0.741884,-0.541926 -0.250591,-0.361297 -0.28386,-0.522402 -0.315075," +
	"-1.52589 -0.06327,-2.03378 0.23288,-3.033615 1.077963,-3.639283 0.307525," +
	"-0.2204 4.818478,-2.133627 6.017853,-2.552345 0.247872,-0.08654 0.247455," +
	"-0.102501 -0.01855,-0.711959 -0.330395,-0.756986 -0.708622,-2.221756 -0.832676," +
	"-3.224748 -0.05031,-0.406952 -0.133825,-3.078805 -0.185533,-5.937448 -0.0517," +
	"-2.858644 -0.145909,-5.208974 -0.209316,-5.222958 -0.06341,-0.01399 -0.974464," +
	"-0.0493 -2.024551,-0.07845 L 23.247235,38.61921 18.831373,39.8906 C 4.9432155," +
	"43.88916 4.2929558,44.057819 3.4954426,43.86823 2.7487826,43.690732 2.2007966," +
	"42.916622 1.9565564,41.694305 z";

function PlaneObject(icao) {
	// Info about the plane
	this.icao      = icao;
	this.flight    = null;
	this.squawk    = null;
	this.selected  = false;
	
	// Basic location information
	this.altitude  = null;
	this.speed     = null;
	this.track     = null;
	this.position  = null;
	this.sitedist  = null;
	
	// Data packet numbers
	this.messages  = null;
	this.rssi      = null;
	
	// Track history as a series of line segments
	this.track_linesegs = [];
	this.history_size = 0;
	
	// When was this last updated (receiver timestamp)
	this.last_message_time = null;
	this.last_position_time = null;
	
	// When was this last updated (seconds before last update)
	this.seen = null;
	this.seen_pos = null;
	
	// Display info
	this.visible = true;
	this.marker = null;
	this.icon = {
		strokeWeight: 1,
		path: PlaneSvg,
		scale: 0.4,
		fillColor: MarkerColor,
		fillOpacity: 0.9,
		anchor: new google.maps.Point(32, 32), // Set anchor to middle of plane.
		rotation: 0};
}

// Appends data to the running track so we can get a visual tail on the plane
// Only useful for a long running browser session.
PlaneObject.prototype.updateTrack = function(estimate_time) {
	var here = this.position;
	if (!here) {
		return;
	}
	
	if (this.track_linesegs.length == 0) {
		// Brand new track
		//console.log(this.icao + " new track");
		var newseg = {
			track : new google.maps.MVCArray([here,here]),
			line : null,
			head_update : this.last_position_time,
			tail_update : this.last_position_time,
			estimated : false,
			ground : (this.altitude === "ground")
		};
		
		this.track_linesegs.push(newseg);
		this.history_size += 2;
		
		return;
	}
	
	var lastseg = this.track_linesegs[this.track_linesegs.length - 1];
	var lastpos = lastseg.track.getAt(lastseg.track.getLength() - 1);
	var elapsed = (this.last_position_time - lastseg.head_update);
	
	var new_data = (here !== lastpos);
	var est_track = (elapsed > estimate_time);
	var ground_track = (this.altitude === "ground");
	
	if (!new_data) {
		return false;
	}
	
	if (est_track) {
		if (!lastseg.estimated) {
			// >5s gap in data, create a new estimated segment
			//console.log(this.icao + " switching to estimated");
			this.track_linesegs.push({
				track : new google.maps.MVCArray([lastpos, here]),
				line : null,
				head_update : this.last_position_time,
				estimated : true });
			
			this.history_size += 2;
			
			return true;
		}
		
		// Append to ongoing estimated line
		//console.log(this.icao + " extending estimated (" + lastseg.track.getLength() + ")");
		lastseg.track.push(here);
		lastseg.head_update = this.last_position_time;
		this.history_size++;
		
		return true;
	}
	
	if (lastseg.estimated) {
		// We are back to good data.
		//console.log(this.icao + " switching to good track");
		this.track_linesegs.push({
			track : new google.maps.MVCArray([lastpos, here]),
			line : null,
			head_update : this.last_position_time,
			tail_update : this.last_position_time,
			estimated : false,
			ground : (this.altitude === "ground") });
		
		this.history_size += 2;
		
		return true;
	}
	
	if ( (lastseg.ground && this.altitude !== "ground") || (!lastseg.ground && this.altitude === "ground") ) {
		//console.log(this.icao + " ground state changed");
		// Create a new segment as the ground state changed.
		// assume the state changed halfway between the two points
		var midpoint = google.maps.geometry.spherical.interpolate(lastpos,here,0.5);
		lastseg.track.push(midpoint);
		
		this.track_linesegs.push({
			track : new google.maps.MVCArray([midpoint,here,here]),
			line : null,
			head_update : this.last_position_time,
			tail_update : this.last_position_time,
			estimated : false,
			ground : (this.altitude === "ground") });
		
		this.history_size += 4;
		
		return true;
	}
	
	// Add more data to the existing track.
	// We only retain some historical points, at 5+ second intervals,
	// plus the most recent point
	if (this.last_position_time - lastseg.tail_update >= 5) {
		// enough time has elapsed; retain the last point and add a new one
		//console.log(this.icao + " retain last point");
		lastseg.track.push(here);
		lastseg.tail_update = lastseg.head_update;
		this.history_size ++;
	} else {
		// replace the last point with the current position
		lastseg.track.setAt(lastseg.track.getLength()-1, here);
	}
	
	lastseg.head_update = this.last_position_time;
	
	return true;
};

// This is to remove the line from the screen if we deselect the plane
PlaneObject.prototype.clearLines = function() {
	for (var i = 0; i < this.track_linesegs.length; ++i) {
		var seg = this.track_linesegs[i];
		if (seg.line !== null) {
			seg.line.setMap(null);
			seg.line = null;
		}
	}
};

PlaneObject.prototype.updateIcon = function() {
	var col = MarkerColor;
	
	// If this marker is selected we should make it lighter than the rest.
	if (this.selected) {
		col = SelectedColor;
	}
	
	// If we have not seen a recent position update, change color
	if (this.seen_pos > 15) {
		col = StaleColor;
	}
	
	// If the squawk code is one of the international emergency codes,
	// match the info window alert color.
	if (this.squawk in SpecialSquawks) {
		col = SpecialSquawks[this.squawk].markerColor;
	}
	
	var weight = this.selected ? 2 : 1;
	var rotation = (this.track === null ? 0 : this.track);
	
	if (col === this.icon.fillColor && weight === this.icon.strokeWeight && rotation === this.icon.rotation) {
		return false;  // no changes
	}
	
	this.icon.fillColor = col;
	this.icon.strokeWeight = weight;
	this.icon.rotation = rotation;
	
	if (this.marker) {
		this.marker.setIcon(this.icon);
	}
	
	return true;
};

// Update our data
PlaneObject.prototype.updateData = function(receiver_timestamp, data) {
	// Update all of our data
	this.messages	= data.messages;
	this.rssi       = data.rssi;
	this.last_message_time = receiver_timestamp - data.seen;
	
	if (typeof data.altitude !== "undefined") {
		this.altitude	= data.altitude;
	}
	
	if (typeof data.vert_rate !== "undefined") {
		this.vert_rate	= data.vert_rate;
	}
	
	if (typeof data.speed !== "undefined") {
		this.speed	= data.speed;
	}
	
	if (typeof data.track !== "undefined") {
		this.track	= data.track;
	}
	
	if (typeof data.lat !== "undefined") {
		this.position   = new google.maps.LatLng(data.lat, data.lon);
		this.last_position_time = receiver_timestamp - data.seen_pos;
	
		if (SitePosition !== null) {
			this.sitedist = google.maps.geometry.spherical.computeDistanceBetween (SitePosition, this.position);
		}
	}
	
	if (typeof data.flight !== "undefined") {
		this.flight	= data.flight;
	}
	
	if (typeof data.squawk !== "undefined") {
		this.squawk	= data.squawk;
	}
};

PlaneObject.prototype.updateTick = function(receiver_timestamp, last_timestamp) {
	// recompute seen and seen_pos
	this.seen = receiver_timestamp - this.last_message_time;
	this.seen_pos = (this.last_position_time === null ? null : receiver_timestamp - this.last_position_time);
	
	// If no packet in over 58 seconds, clear the plane.
	if (this.seen > 58) {
		if (this.visible) {
			//console.log("hiding " + this.icao);
			this.clearMarker();
			this.visible = false;
			if (SelectedPlane == this.icao) {
				selectPlaneByHex(null,false);
			}
		}
	} else {
		this.visible = true;
		if (this.position !== null) {
			if (this.updateTrack(receiver_timestamp - last_timestamp + 5)) {
				this.updateLines();
				this.updateMarker(true);
			} else {
				this.updateMarker(false); // didn't move
			}
		}
	}
};

PlaneObject.prototype.clearMarker = function() {
	if (this.marker) {
		this.marker.setMap(null);
		google.maps.event.clearListeners(this.marker, 'click');
		this.marker = null;
	}
};

// Update our marker on the map
PlaneObject.prototype.updateMarker = function(moved) {
	if (!this.visible) {
		this.clearMarker();
		return;
	}
	
	if (this.marker) {
		if (moved) {
			this.marker.setPosition(this.position);
		}
		this.updateIcon();
	} else {
		this.updateIcon();
		this.marker = new google.maps.Marker({
			position: this.position,
			map: GoogleMap,
			icon: this.icon,
			visible: true
		});
	
		// Trap clicks for this marker.
		google.maps.event.addListener(this.marker, 'click', selectPlaneByHex.bind(undefined,this.icao,false));
		google.maps.event.addListener(this.marker, 'dblclick', selectPlaneByHex.bind(undefined,this.icao,true));
	}
	
	// Setting the marker title
	var title = (this.flight === null || this.flight.length == 0) ? this.icao : (this.flight+' ('+this.icao+')');
	if (title !== this.marker.title) {
		this.marker.setTitle(title);
	}
};

// Update our planes tail line,
PlaneObject.prototype.updateLines = function() {
	if (!this.selected) {
		return;
	}
	
	for (var i = 0; i < this.track_linesegs.length; ++i) {
		var seg = this.track_linesegs[i];
		if (seg.line === null) {
		// console.log("create line for seg " + i + " with " + seg.track.getLength() + " points" + (seg.estimated ? " (estimated)" : ""));
		// for (var j = 0; j < seg.track.getLength(); j++) {
		//         console.log("  point " + j + " at " + seg.track.getAt(j).lat() + "," + seg.track.getAt(j).lng());
		// }
		
			if (seg.estimated) {
				var lineSymbol = {
					path: 'M 0,-1 0,1',
					strokeOpacity : 1,
					strokeColor : '#804040',
					strokeWeight : 2,
					scale: 2
				};
			
				seg.line = new google.maps.Polyline({
					path: seg.track,
					strokeOpacity: 0,
					icons: [{
					icon: lineSymbol,
					offset: '0',
					repeat: '10px' }],
					map : GoogleMap });
			} else {
				seg.line = new google.maps.Polyline({
					path: seg.track,
					strokeOpacity: 1.0,
					strokeColor: (seg.ground ? '#408040' : '#000000'),
					strokeWeight: 3,
					map: GoogleMap });
			}
		}
	}
};

PlaneObject.prototype.destroy = function() {
	this.clearLines();
	this.clearMarker();
};