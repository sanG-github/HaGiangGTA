[out:json][timeout:120];
(
  way["building"](23.215,105.371,23.275,105.436);
  way["building"](23.158,105.183,23.180,105.206);
  way["landuse"](23.215,105.371,23.275,105.436);
  way["landuse"](23.158,105.183,23.180,105.206);
  way["natural"~"^(wood|scrub|grassland|bare_rock|cliff)$"](23.215,105.371,23.275,105.436);
  way["natural"~"^(wood|scrub|grassland|bare_rock|cliff)$"](23.158,105.183,23.180,105.206);
);
out geom tags;
