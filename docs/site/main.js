"use strict";
const group = document.getElementById("threads");
if (group) {
  for (let i = 0; i < 25; i++) {
    const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", `M 16 ${50 + i * 10} C 160 ${40 + i * 7} 190 ${370 - i * 5} 424 ${350 - i * 9}`);
    path.setAttribute("opacity", String(0.35 + i / 50));
    group.append(path);
  }
}
