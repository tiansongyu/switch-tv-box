function toLong(s) {
  var ret = s.trim();
  try {
    if (ret.startsWith("0x")) {
      return Long.fromString(ret.substring(2), 16);
    }
    return Long.fromString(ret, 10);
  } catch (e) {
    return Long.ZERO;
  }
}
function setBitClassName(prefix, value, selected, notSelected) {
  for (var i=0; i<65; i++) {
    var b = Long.ONE.shiftLeft(i);
    var el = document.getElementById(prefix + "-bit" + i);
    if (el) {
      if (value.and(b).equals(b)) {
        el.className = selected;
      } else {
        el.className = notSelected;
      }
    }
  }
}
function updateFlagTable(prefix, value) {
  if (value.equals(Long.ZERO)) {
    document.getElementById(prefix + "-output").className = "noscript";
    setBitClassName(prefix, Long.ZERO, "", "")
    document.getElementById(prefix).className = "";
  } else {
    document.getElementById(prefix + "-output").className = "";
    setBitClassName(prefix, value, "selected", "notSelected")
    document.getElementById(prefix).className = "selecting";
  }

  document.getElementById(prefix + "-dec").innerHTML = value.toString(10);
  document.getElementById(prefix + "-hex").innerHTML = "0x" + value.toString(16);
}

function exampleChanged(prefix, fn) {
  var el = document.getElementById(prefix + "-examples");
  document.getElementById(prefix + "-input").value = el.value;
  fn(el);
}

function inputChanged(prefix, fn, bit) {
  var el = document.getElementById(prefix + "-input");
  document.getElementById(prefix + "-examples").value = "";
  document.getElementById(prefix + "-interactive").className = "";
  fn(el, bit);
}

function flipBit(prefix, fn, bit) {
  var el = document.getElementById(prefix + "-input");
  document.getElementById(prefix + "-examples").value = "";
  document.getElementById(prefix + "-interactive").className = "";
  fn(el, bit)
}

function updateFeatures(el, bit) {
  var featuresS = el.value;
  var comma = featuresS.indexOf(",");
  var features = 0;
  if (comma >= 0) {
    var high = toLong(featuresS.substring(comma + 1));
    var low = toLong(featuresS.substring(0, comma));
    features = high.shiftLeft(32).or(low);
  } else {
    features = toLong(featuresS);
  }
  if (typeof bit != "undefined") {
    var b = Long.ONE.shiftLeft(bit);
    features = features.xor(b);
    el.value = "0x" + features.toString(16);
  }
  updateFlagTable("airplay-features", features);
  var txt = "0x" + features.and(Long.fromBits(0xffffffff, 0, true)).toString(16) +
    ",0x" + features.shiftRight(32).toString(16);
  document.getElementById("airplay-features-txt").innerHTML = txt;
}

function updateSystemFlags(el, bit) {
  var systemFlagsS = el.value;
  var systemFlags = 0;
  systemFlags = toLong(systemFlagsS);
  if (typeof bit != "undefined") {
    var b = Long.ONE.shiftLeft(bit);
    systemFlags = systemFlags.xor(b);
    el.value = "0x" + systemFlags.toString(16);
  }
  updateFlagTable("airplay-system-flags", systemFlags);
}

function installHandlers(prefix, fn) {
  var input = function() {
    inputChanged(prefix, fn);
  }
  var example = function() {
    exampleChanged(prefix, fn);
  }
  var changeBit = function(ev) {
    var target = ev.target;
    while (target != null && target.tagName != "TR") {
      target = target.parentElement;
    }
    if (target == null) {
      return;
    }
    var bit = parseInt(target.id.substring(prefix.length + "-bit".length));
    inputChanged(prefix, fn, bit);
  }
  var el = document.getElementById(prefix + "-input");
  el.onkeyup = input;
  el.onpaste = input;
  var el = document.getElementById(prefix + "-examples");
  el.onchange = example;
  var el = document.getElementById(prefix + "-bit0");
  el.parentNode.onclick = changeBit;
  input();
}
installHandlers("airplay-features", updateFeatures)
installHandlers("airplay-system-flags", updateSystemFlags)
