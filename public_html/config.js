// --------------------------------------------------------
//
// This file is to configure the configurable settings.
// Load this file before script.js file at gmap.html.
//
// --------------------------------------------------------

// -- Formatting Settings ---------------------------------
// Enable or disable Metric and`/or Imperial formatting
// Controls the units shown on the map and in the table
// The PreferMetric is used to determine which value to
// show in the compact mode when both Imperial and Metric
// are enabled.
PreferMetric = true;
EnableMetric = true;
EnableImperial = false;

// -- Highscore Settings ----------------------------------
// Enable or disable highscore storage through cookies
SaveHighscores = true;
// how many days must the cookies last
SaveLastsForDays = 365
// how many previous cookies to keep?
SaveLastNumDays = 31;

// -- Map settings ----------------------------------------
// These settings are overridden by any position information
// provided by dump1090 itself. All positions are in decimal
// degrees.

// Default center of the map.
DefaultCenterLat = 45.0;
DefaultCenterLon = 9.0;
// The google maps zoom level, 0 - 16, lower is further out
DefaultZoomLvl   = 7;

SiteShow    = false;           // true to show a center marker
SiteLat     = 45.0;            // position of the marker
SiteLon     = 9.0;
SiteName    = "My Radar Site"; // tooltip of the marker

// Enable or disable auto-selecting of the terrain for default map.
AutoLoadTerrainMap = true;

// -- Marker settings -------------------------------------
// The default marker color
MarkerColor	  = "rgb(127, 127, 127)";
SelectedColor = "rgb(225, 225, 225)";
StaleColor = "rgb(190, 190, 190)";


SiteCircles = true; // true to show circles (only shown if the center marker is shown)
// In nautical miles or km (depending settings value 'Metric')
SiteCirclesDistances = new Array(50,100,150,200);

// Show the clocks at the top of the righthand pane? You can disable the clocks if you want here
ShowClocks = true;

// Controls page title, righthand pane when nothing is selected
PageName = "DUMP1090";
