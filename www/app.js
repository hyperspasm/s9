// A control for every parameter in the manifest, live over pn: this page is
// generated from the manifest the server serves, so it fits whatever the
// running program's schema is.
import { Client, connect, parseManifest } from "/pn/pn.js";

const status = document.getElementById("status");
const banner = document.getElementById("banner");
const main = document.getElementById("params");

const manifest = parseManifest(await (await fetch("/s9.manifest")).text());

// --- controls: each knows how to show a value and what to send on change

const controls = new Map();

function row(entry, input, value) {
  const div = document.createElement("div");
  div.className = "param";
  const label = document.createElement("label");
  label.textContent = entry.name;
  div.append(label, input, value ?? document.createElement("span"));
  return div;
}

function number(entry, client, isInt) {
  const input = document.createElement("input");
  input.type = "range";
  input.min = entry.min;
  input.max = entry.max;
  input.step = isInt ? Math.max(1, entry.step) : entry.step || (entry.max - entry.min) / 1000;
  const value = document.createElement("span");
  value.className = "value";
  const show = v => { value.textContent = isInt ? String(v) : Number(v).toPrecision(4); };
  input.oninput = () => {
    show(input.value);
    if (isInt) client.setInt(entry.path, Number(input.value));
    else client.setFloat(entry.path, Number(input.value));
  };
  return { element: row(entry, input, value), set(v) { input.value = v; show(v); } };
}

function boolean(entry, client) {
  const input = document.createElement("input");
  input.type = "checkbox";
  input.onchange = () => client.setBool(entry.path, input.checked);
  return { element: row(entry, input), set(v) { input.checked = v; } };
}

function enumeration(entry, client) {
  const select = document.createElement("select");
  entry.labels.forEach((label, i) => {
    const option = document.createElement("option");
    option.value = i;
    option.textContent = label;
    select.append(option);
  });
  select.onchange = () => client.setEnum(entry.path, Number(select.value));
  return { element: row(entry, select), set(v) { select.value = v; } };
}

function string(entry, client) {
  const input = document.createElement("input");
  input.type = "text";
  input.maxLength = entry.max_len;
  input.onchange = () => client.setString(entry.path, input.value);
  return { element: row(entry, input), set(v) { input.value = v; } };
}

function action(entry, client) {
  const button = document.createElement("button");
  button.textContent = entry.name;
  button.onclick = () => client.action(entry.path);
  const div = row(entry, button);
  div.querySelector("label").textContent = "";
  return { element: div, set() {} };
}

const MAKERS = {
  int: (e, c) => number(e, c, true),
  float: (e, c) => number(e, c, false),
  bool: boolean,
  enum: enumeration,
  string,
  action,
};

// --- page: one section per group, in manifest order

const client = new Client({
  onUpdate(path, value) {
    const control = controls.get(path);
    if (control) control.set(value);
  },
});

let section = null;
let sectionName = null;
for (const entry of manifest.entries) {
  const group = entry.path.slice(0, entry.path.lastIndexOf("/"));
  if (group !== sectionName) {
    section = document.createElement("section");
    const h = document.createElement("h2");
    h.textContent = group;
    section.append(h);
    main.append(section);
    sectionName = group;
  }
  const control = MAKERS[entry.type](entry, client);
  controls.set(entry.path, control);
  section.append(control.element);
}

const url = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}`;
connect(client, url, {
  async onConnect() {
    status.textContent = "connected";
    status.className = "connected";
    const ok = await client.checkSchema(manifest, { onMismatch: "ignore" }).catch(() => false);
    banner.hidden = ok;
    banner.textContent = ok ? "" : "The server's parameters differ from this page's; controls may not match.";
    client.get(manifest.root);
  },
  onDisconnect() {
    status.textContent = "disconnected, retrying";
    status.className = "";
  },
});
