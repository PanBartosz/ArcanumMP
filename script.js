/* Native video controls, no tracking, embeds or autoplay. */
const trailer = document.querySelector("#trailer");
const demo = document.querySelector("#layout-demo");
for (const player of [trailer, demo]) {
  if (!player) continue;
  player.addEventListener("play", () => {
    for (const other of [trailer, demo])
      if (other && other !== player) other.pause();
  });
}
