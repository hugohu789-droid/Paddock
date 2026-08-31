# M3 — Real ground and livestock

*August 2026*

The simulator runs on ground that was surveyed rather than invented. `gis/`
reads LINZ LiDAR and cadastre through GDAL, PROJ transforms them into NZTM2000
to under a millimetre, and paddock polygons become raster masks the pasture
model steps cell by cell. Sheep graze it, driven by species TOML and moved by a
farmer who decides rather than follows. There is a 3D terrain view — and it all
still runs where GDAL is absent, drawing the farm flat and saying so.

**One problem worth writing down.** The terrain model had never run.
`Farm::set_slopes` had no callers anywhere in the repository. Slope and aspect
were computed, unit-tested, documented — and every scenario stepped a farm the
terrain never reached. Nothing failed, because nothing was asking. Wiring it
moved growth on rolling ground by a few per cent: small enough that no test
would have caught it, large enough to be wrong. That shape has since appeared
three more times here — an energy term with no input, a data file nothing
loaded, a unit conversion nothing consumed. The cheap test: ask of every new
quantity which caller reads it, before committing rather than after.

**One New Zealand thing I did not know.** LINZ publishes its 1 m elevation as
tiles inside collections, and a collection's bounding box is the union of its
tiles — which can have holes, so "does this collection cover my farm" answers
yes when the truth is no. Worse, a tile covering your coordinate may not contain
your *farm*: the first Waikato tile I fetched ran out 511 m west of the block I
wanted, and reported success. Scenario files now record the tile extent and the
margin at the block's nearest edge, so it is visibly checked.
