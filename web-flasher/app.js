import "./vendor/esp-web-tools/install-button.js";

const checkbox = document.querySelector("#confirm-board");
const connect = document.querySelector("#connect");
const browserStatus = document.querySelector("#browser-status");
const integrity = document.querySelector("#integrity-status");
const retry = document.querySelector("#retry");
const supported = window.isSecureContext && "serial" in navigator;
let verified = false;

function updateButton() {
  connect.disabled = !supported || !verified || !checkbox.checked;
  browserStatus.textContent = !window.isSecureContext
    ? "USB flashing requires HTTPS. Open the published site or localhost."
    : !supported
      ? "Open this page in desktop Chrome or Edge for USB flashing. iPhone/iPad and Safari are not supported."
      : !verified
        ? "USB serial is available. Waiting for firmware verification."
        : !checkbox.checked
          ? "Confirm your board and backup above to connect."
          : "Ready. Your browser will ask you to choose a USB serial port.";
}

async function verifyFirmware() {
  verified = false;
  retry.hidden = true;
  integrity.classList.remove("error");
  integrity.textContent = "Verifying the release download…";
  updateButton();
  try {
    const responses = await Promise.all([fetch("release.json", {cache: "no-cache"}), fetch("manifest.json", {cache: "no-cache"})]);
    if (responses.some(response => !response.ok)) throw new Error("Release metadata is unavailable");
    const [release, manifest] = await Promise.all(responses.map(response => response.json()));
    const builds = manifest.builds;
    if (!Array.isArray(builds) || builds.length !== 1 || builds[0].chipFamily !== "ESP32-S3" ||
        builds[0].parts.length !== 1 || builds[0].parts[0].offset !== 0 ||
        builds[0].parts[0].path !== release.path || manifest.version !== release.version ||
        !/^firmware\/[a-zA-Z0-9._-]+\.bin$/.test(release.path) || !/^[0-9a-f]{64}$/.test(release.sha256)) {
      throw new Error("Unexpected firmware manifest");
    }
    const response = await fetch(release.path);
    if (!response.ok) throw new Error("Firmware download is unavailable");
    const data = await response.arrayBuffer();
    const digest = await crypto.subtle.digest("SHA-256", data);
    const hash = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
    if (hash !== release.sha256 || data.byteLength !== release.bytes) throw new Error("Firmware checksum mismatch");
    document.querySelector("#version").textContent = release.version;
    document.querySelector("#download").href = release.path;
    document.querySelector("#release-link").href = release.release_url;
    integrity.textContent = `Firmware verified · SHA-256 ${hash.slice(0, 12)}… · ${(data.byteLength / 1024 / 1024).toFixed(2)} MB`;
    verified = true;
  } catch (error) {
    integrity.textContent = `${error.message}. Installation is disabled. Download the release or retry.`;
    integrity.classList.add("error");
    retry.hidden = false;
  }
  updateButton();
}
checkbox.addEventListener("change", updateButton);
retry.addEventListener("click", verifyFirmware);
verifyFirmware();
