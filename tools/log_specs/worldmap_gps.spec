# worldmap_gps.spec - the world map drives a GPS route to a race and reports arrival.
#   python3 -I tools/log_assert.py tools/log_specs/worldmap_gps.spec <gate>/worldmap_gps.log
# Ported from the S2/S3 run (reports/worldmap/s3/live.log), race-destination case only.
# --world-map-dev prints one 'icon=' line per destination (kind=1 is a race, kind=2 a shop) and the
# run cycles them with TAB; 'selected' names the icon the request was really made for, so the id
# ties the choice to an icon the map actually offered. The WM probe line is the host's own view of
# the guidance: a node count above zero and a marker are what the HUD arrow and the minimap read,
# and the route must survive closing the map (active=1 again at a later frame). F8 teleports the car
# to the target pose, which is the hook for arrival: reaching the zone has to clear route and marker.
capture ICON /world map: selected kind=1 id=([0-9a-f]{8}) x=-?\d+\.\d+ y=-?\d+\.\d+/
expect 1 /world map: opened icons=\d+ player=\(-?\d+\.\d+ -?\d+\.\d+\)/
expect >=1 /world map: icon=\d+ kind=1 id={ICON} canvas=\(-?\d+\.\d+ -?\d+\.\d+\)/
expect 1 /GPS route: \d+ nodes, \d+ m, \d+ ms/
capture NODES /GPS route: ([1-9]\d*) nodes/
range NODES 1 100000
expect >=1 /WM f=\d+ active=1 kind=1 id={ICON} route={NODES} marker=1/
expect >=1 /world map: test arrival kind=1 id={ICON} at=\(-?\d+\.\d+ -?\d+\.\d+\)/
expect >=1 /world map: destination zone reached kind=1 id={ICON} route=0 marker=0/
order /world map: opened/ /world map: selected kind=1 id={ICON}/ /GPS route: {NODES} nodes/ /world map: test arrival/ /world map: destination zone reached/
forbid /world map: destination no longer available/
forbid /inconsistent career icon snapshot/
forbid /Segmentation|ASan|AddressSanitizer|LeakSanitizer|abort/
