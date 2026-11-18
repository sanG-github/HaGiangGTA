[out:json][timeout:120];
(
  way["highway"~"^(trunk|primary|secondary|tertiary|unclassified|residential|track|service|path)$"](23.13,105.15,23.30,105.52);
  way["waterway"~"^(river|stream)$"](23.13,105.15,23.30,105.52);
  way["natural"="water"](23.13,105.15,23.30,105.52);
  relation["natural"="water"](23.13,105.15,23.30,105.52);
  way["waterway"="dam"](23.13,105.15,23.30,105.52);
  node["tourism"](23.13,105.15,23.30,105.52);
  node["name"~"Thẩm Mã|Mã Pí Lèng|Tà Làng|Tu Sản|Mỏm Đá|Nho Quế",i](23.13,105.15,23.30,105.52);
);
out geom tags;
