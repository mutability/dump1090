// formatting helpers

// track in degrees (0..359)
function format_track_brief(track) {
	if (track === null){
		return "";
	}
	
	return Math.round(track);
}

// track in degrees (0..359)
function format_track_long(track) {
	if (track === null){
		return "n/a";
	}
	
	var trackDir = Math.floor((360 + track % 360 + 22.5) / 45) % 8;
	return Math.round(track) + DEGREES + NBSP + "(" + TrackDirections[trackDir] + ")";
}

// altitude (input: alt in feet)
// brief will always show either Metric or Imperial
function format_altitude_brief(alt, vr) {
	var alt_text;
	
	// 4 cases possible:
	// 1: EnableMetric = true  | EnableImperial = false | PreferMetric = (Dont Care) -> display metric
	// 2: EnableMetric = false | EnableImperial = true  | PreferMetric = (Dont Care) -> display imperial
	// 3: EnableMetric = true  | EnableImperial = true  | PreferMetric = true        -> display metric
	// 4: EnableMetric = true  | EnableImperial = true  | PreferMetric = false       -> display imperial
	
	if (alt === null){
		return "";
	} else if (alt === "ground"){
		return "ground";
	}
	
	if ((EnableMetric && !EnableImperial) || (PreferMetric && EnableMetric && EnableImperial)){
		alt_text = Math.round(alt / 3.2828) + NBSP; // Altitude to meters
	} else {
		alt_text = Math.round(alt) + NBSP;
	}
	
	// Vertical Rate Triangle
	if (vr > 128){
		return alt_text + UP_TRIANGLE;
	} else if (vr < -128){
		return alt_text + DOWN_TRIANGLE;
	} else {
		return alt_text + NBSP;
	}
}

// alt in ft
function format_altitude_long(alt, vr) {
	var alt_text = "";
	
	if (alt === null) {
		return "n/a";
	} else if (alt === "ground") {
		return "on ground";
	}
	
	// If we only want to see one of the two types
	if((EnableMetric && !EnableImperial) || (!EnableMetric && EnableImperial)){
		if(EnableMetric){
			alt_text = Math.round(alt / 3.2828) + " m";
		}
		else{
			alt_text = Math.round(alt) + " ft";
		}
	}
	else{ // we want to see both, check PreferMetric for what order
		if (PreferMetric) {
			alt_text = Math.round(alt / 3.2828) + " m | " + Math.round(alt) + " ft";
		} else {
			alt_text = Math.round(alt) + " ft | " + Math.round(alt / 3.2828) + " m";
		}
	}
	
	if (vr > 128) {
		return UP_TRIANGLE + NBSP + alt_text;
	} else if (vr < -128) {
		return DOWN_TRIANGLE + NBSP + alt_text;
	} else {
		return alt_text;
	}
}

//input: speed in kts
function format_speed_brief(speed) {
	if (speed === null) {
		return "";
	}
	
	if ((EnableMetric && !EnableImperial) || (PreferMetric && EnableMetric && EnableImperial)){
		return Math.round(speed * 1.852); // knots to kilometers per hour
	} else {
		return Math.round(speed); // knots
	}
}

// speed in kts
function format_speed_long(speed) {	
	if (speed === null) {
		return "n/a";
	}
	
	// If we only want to see one of the two types
	if((EnableMetric && !EnableImperial) || (!EnableMetric && EnableImperial)){
		if(EnableMetric){
			return Math.round(speed * 1.852) + " km/h";
		}
		else{
			return Math.round(speed) + " kt";
		}
	}
	else{ // we want to see both, check PreferMetric for what order
		if (PreferMetric) {
			return Math.round(speed * 1.852) + " km/h | " + Math.round(speed) + " kt";
		} else {
			return Math.round(speed) + " kt | " + Math.round(speed * 1.852) + " km/h";
		}
	}
	
}

// dist in meters
function format_distance_brief(dist) {
	if (dist === null) {
		return "";
	}

	if ((EnableMetric && !EnableImperial) || (PreferMetric && EnableMetric && EnableImperial)){
		return (dist/1000).toFixed(1); // meters to kilometers
	} else {
		return (dist/1852).toFixed(1); // meters to nautocal miles
	}
}

// dist in metres
function format_distance_long(dist) {	
	if (dist === null) {
		return "n/a";
	}
	
	// If we only want to see one of the two types
	if((EnableMetric && !EnableImperial) || (!EnableMetric && EnableImperial)){
		if(EnableMetric){
			return (dist/1000).toFixed(1) + " km";
		}
		else{
			return (dist/1852).toFixed(1) + " NM";
		}
	}
	else{ // we want to see both, check PreferMetric for what order
		if (PreferMetric) {
			return (dist/1000).toFixed(1) + " km | " + (dist/1852).toFixed(1) + " NM";
		} else {
			return (dist/1852).toFixed(1) + " NM | " + (dist/1000).toFixed(1) + " km";
		}
	}
}

// p as a LatLng
function format_latlng(p) {
	return p.lat().toFixed(3) + DEGREES + "," + NBSP + p.lng().toFixed(3) + DEGREES;
}