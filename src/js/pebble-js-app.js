var THEME_STORAGE_KEY = 'pill-reminder-theme';
var LANGUAGE_STORAGE_KEY = 'pill-reminder-language-v1';
var DISPLAY_STORAGE_KEY = 'pill-reminder-display-v1';
var LEGACY_MEDICATION_STORAGE_KEY = 'pill-reminder-medication-v1';
var MEDICATIONS_STORAGE_KEY = 'pill-reminder-medications-v2';
var DAYPART_STORAGE_KEY = 'pill-reminder-dayparts-v1';
var ALARM_STORAGE_KEY = 'pill-reminder-alarm-v1';
var SETTINGS_TRANSACTION_STORAGE_KEY = 'pill-reminder-settings-transaction-v1';

var MAX_MEDICATIONS = 16;
var MINUTES_PER_DAY = 1440;

var THEME_KEY = 0;
var MED_NAME_KEY = 1;
var MED_QUANTITY_KEY = 2;
var MED_TIME_KEY = 3;
var MED_SCHEDULE_KEY = 4;
var MED_DAY_KEY = 5;
var MED_SYMBOL_KEY = 6;
var MED_ENABLED_KEY = 7;
var MED_INDEX_KEY = 8;
var MED_COUNT_KEY = 9;
var MED_COMMAND_KEY = 10;
var DAYPART_MORNING_KEY = 11;
var DAYPART_NOON_KEY = 12;
var DAYPART_EVENING_KEY = 13;
var DAYPART_NIGHT_KEY = 14;
var MED_SHAPE_KEY = 15;
var MED_COLOR_KEY = 16;
var MED_ICON_SET_KEY = 17;
var AUDIO_VOLUME_KEY = 18;
var VIBRATION_ENABLED_KEY = 19;
var REMINDER_INTERVAL_KEY = 20;
var MED_COLOR2_KEY = 21;
var MED_SIZE_KEY = 22;
var MED_IMPRINT_KEY = 23;
var MED_DOSAGE_KEY = 24;
var MED_EFFECT_KEY = 25;
var SETTINGS_TRANSACTION_KEY = 26;
var SETTINGS_ACK_KEY = 27;
var LANGUAGE_KEY = 28;
var MED_INTERVAL_HOURS_KEY = 29;
var SHOW_JAPANESE_PATTERN_KEY = 30;
var MED_INTERVAL_START_HOUR_KEY = 31;
var MED_INTERVAL_START_MINUTE_KEY = 32;


var COMMAND_RESET = 0;
var COMMAND_ITEM = 1;
var COMMAND_COMMIT = 2;

var LEGACY_DEFAULT_DAYPARTS = {
  morning: 5 * 60,
  noon: 11 * 60,
  evening: 16 * 60,
  night: 21 * 60
};

var DEFAULT_DAYPARTS = {
  morning: 6 * 60,
  noon: 12 * 60,
  evening: 18 * 60,
  night: 22 * 60
};

var DEFAULT_AUDIO_VOLUME = 100;
var REMINDER_INTERVALS = [1, 5, 10, 15, 20, 30, 60];
var DEFAULT_REMINDER_INTERVAL = 15;
var MEDICATION_INTERVAL_HOURS = [2, 3, 4, 6, 8, 12];
var DEFAULT_MEDICATION_INTERVAL_HOURS = 4;
var DEFAULT_MEDICATION_INTERVAL_START = 8 * 60;

var DEFAULT_MEDICATION = {
  name: 'Xarelto',
  dosage: '20 mg',
  effect: 'Blutverdünner',
  quantity: 1,
  time: 0,
  schedule: 0,
  day: 0,
  symbol: 0,
  shape: 2,
  color: 255,
  color2: -1,
  size: 100,
  imprint: '20',
  iconSet: true,
  enabled: true,
  intervalHours: DEFAULT_MEDICATION_INTERVAL_HOURS,
  intervalStart: DEFAULT_MEDICATION_INTERVAL_START
};

function currentTheme() {
  var stored = localStorage.getItem(
    THEME_STORAGE_KEY
  );

  return (
    stored === 'light' ||
    stored === 'dark'
  )
    ? stored
    : 'dark';
}

function systemLanguage() {
  var locale = '';

  try {
    if (
      typeof Pebble !== 'undefined' &&
      typeof Pebble.getActiveWatchInfo === 'function'
    ) {
      var watchInfo = Pebble.getActiveWatchInfo();

      if (
        watchInfo &&
        typeof watchInfo.language === 'string'
      ) {
        locale = watchInfo.language;
      }
    }
  } catch (error) {
    console.log(
      'Could not read watch language: ' +
      error.message
    );
  }

  if (
    !locale &&
    typeof navigator !== 'undefined'
  ) {
    locale =
        navigator.language ||
        navigator.userLanguage ||
        '';
  }

  return /^de(?:[_-]|$)/i.test(locale)
      ? 'de'
      : 'en';
}

function currentLanguage() {
  var stored = localStorage.getItem(
    LANGUAGE_STORAGE_KEY
  );

  if (stored === 'de' || stored === 'en') {
    return stored;
  }

  return systemLanguage();
}

function normalizeDisplaySettings(value) {
  return {
    showPattern:
        !value ||
        value.showPattern !== false
  };
}

function currentDisplaySettings() {
  var stored = localStorage.getItem(
    DISPLAY_STORAGE_KEY
  );

  if (!stored) {
    return normalizeDisplaySettings(null);
  }

  try {
    return normalizeDisplaySettings(
      JSON.parse(stored)
    );
  } catch (error) {
    console.log(
      'Could not read display settings: ' +
      error.message
    );
    return normalizeDisplaySettings(null);
  }
}

function cloneDefaultDayparts() {
  return {
    morning: DEFAULT_DAYPARTS.morning,
    noon: DEFAULT_DAYPARTS.noon,
    evening: DEFAULT_DAYPARTS.evening,
    night: DEFAULT_DAYPARTS.night
  };
}

function daypartsValid(value) {
  return (
    value &&
    integerInRange(
      value.morning,
      0,
      MINUTES_PER_DAY - 1
    ) &&
    integerInRange(
      value.noon,
      0,
      MINUTES_PER_DAY - 1
    ) &&
    integerInRange(
      value.evening,
      0,
      MINUTES_PER_DAY - 1
    ) &&
    integerInRange(
      value.night,
      0,
      MINUTES_PER_DAY - 1
    ) &&
    value.morning < value.noon &&
    value.noon < value.evening &&
    value.evening < value.night
  );
}

function normalizeDayparts(value) {
  if (!daypartsValid(value)) {
    return cloneDefaultDayparts();
  }

  return {
    morning: value.morning,
    noon: value.noon,
    evening: value.evening,
    night: value.night
  };
}

function currentDayparts() {
  var stored = localStorage.getItem(
    DAYPART_STORAGE_KEY
  );

  if (!stored) {
    return cloneDefaultDayparts();
  }

  try {
    var normalized = normalizeDayparts(
      JSON.parse(stored)
    );

    if (
      normalized.morning === LEGACY_DEFAULT_DAYPARTS.morning &&
      normalized.noon === LEGACY_DEFAULT_DAYPARTS.noon &&
      normalized.evening === LEGACY_DEFAULT_DAYPARTS.evening &&
      normalized.night === LEGACY_DEFAULT_DAYPARTS.night
    ) {
      return cloneDefaultDayparts();
    }

    return normalized;
  } catch (error) {
    console.log(
      'Could not read dayparts: ' +
      error.message
    );

    return cloneDefaultDayparts();
  }
}

function reminderIntervalIndex(value) {
  for (var index = 0; index < REMINDER_INTERVALS.length; index++) {
    if (REMINDER_INTERVALS[index] === value) {
      return index;
    }
  }

  return -1;
}

function normalizeAlarmSettings(value) {
  var rawVolume = value && integerInRange(
    value.audioVolume,
    0,
    100
  )
    ? value.audioVolume
    : DEFAULT_AUDIO_VOLUME;

  var audioEnabled =
      value &&
      typeof value.audioEnabled === 'boolean'
          ? value.audioEnabled
          : rawVolume > 0;

  if (!value) {
    audioEnabled = true;
  }

  var volume =
      rawVolume > 0
          ? rawVolume
          : DEFAULT_AUDIO_VOLUME;

  var reminderInterval =
      value && reminderIntervalIndex(
        value.reminderInterval
      ) >= 0
          ? value.reminderInterval
          : DEFAULT_REMINDER_INTERVAL;

  return {
    audioEnabled: audioEnabled,
    audioVolume: volume,
    vibrationEnabled:
        !value || value.vibrationEnabled !== false,
    reminderInterval: reminderInterval
  };
}

function currentAlarmSettings() {
  var stored = localStorage.getItem(
    ALARM_STORAGE_KEY
  );

  if (!stored) {
    return normalizeAlarmSettings(null);
  }

  try {
    return normalizeAlarmSettings(
      JSON.parse(stored)
    );
  } catch (error) {
    console.log(
      'Could not read alarm settings: ' +
      error.message
    );

    return normalizeAlarmSettings(null);
  }
}

function cloneDefaultMedication() {
  return {
    name: DEFAULT_MEDICATION.name,
    dosage: DEFAULT_MEDICATION.dosage,
    effect: DEFAULT_MEDICATION.effect,
    quantity: DEFAULT_MEDICATION.quantity,
    time: DEFAULT_MEDICATION.time,
    schedule: DEFAULT_MEDICATION.schedule,
    day: DEFAULT_MEDICATION.day,
    symbol: DEFAULT_MEDICATION.symbol,
    shape: DEFAULT_MEDICATION.shape,
    color: DEFAULT_MEDICATION.color,
    color2: DEFAULT_MEDICATION.color2,
    size: DEFAULT_MEDICATION.size,
    imprint: DEFAULT_MEDICATION.imprint,
    iconSet: true,
    enabled: DEFAULT_MEDICATION.enabled,
    intervalHours: DEFAULT_MEDICATION.intervalHours,
    intervalStart: DEFAULT_MEDICATION.intervalStart
  };
}

function blankMedication() {
  return {
    name: '',
    dosage: '',
    effect: '',
    quantity: 1,
    time: 0,
    schedule: 0,
    day: 0,
    symbol: -1,
    shape: -1,
    color: -1,
    color2: -1,
    size: 100,
    imprint: '',
    iconSet: false,
    enabled: false,
    intervalHours: DEFAULT_MEDICATION_INTERVAL_HOURS,
    intervalStart: DEFAULT_MEDICATION_INTERVAL_START
  };
}

function integerInRange(value, minimum, maximum) {
  return (
    typeof value === 'number' &&
    isFinite(value) &&
    Math.floor(value) === value &&
    value >= minimum &&
    value <= maximum
  );
}

function utf8Length(value) {
  return unescape(
    encodeURIComponent(value)
  ).length;
}

function truncateUtf8(value, maximumBytes) {
  while (
    value.length > 0 &&
    utf8Length(value) > maximumBytes
  ) {
    value = value.slice(0, -1);
  }

  return value;
}

function medicationIntervalHoursValid(value) {
  return MEDICATION_INTERVAL_HOURS.indexOf(value) >= 0;
}

function normalizeMedication(value) {
  if (!value || typeof value !== 'object') {
    return cloneDefaultMedication();
  }

  var name = typeof value.name === 'string'
    ? value.name.trim()
    : '';

  name = truncateUtf8(name, 31);

  if (!name) {
    name = DEFAULT_MEDICATION.name;
  }

  var dosage = typeof value.dosage === 'string'
    ? value.dosage.trim()
    : '';

  dosage = truncateUtf8(dosage, 20);

  var effect = typeof value.effect === 'string'
    ? value.effect.trim()
    : '';

  effect = truncateUtf8(effect, 31);

  var quantity = integerInRange(value.quantity, 1, 20)
    ? value.quantity
    : DEFAULT_MEDICATION.quantity;

  var time = integerInRange(value.time, 0, 4)
    ? value.time
    : DEFAULT_MEDICATION.time;

  var intervalHours =
      medicationIntervalHoursValid(value.intervalHours)
          ? value.intervalHours
          : DEFAULT_MEDICATION_INTERVAL_HOURS;

  var intervalStart = integerInRange(
    value.intervalStart,
    0,
    MINUTES_PER_DAY - 1
  )
    ? value.intervalStart
    : DEFAULT_MEDICATION_INTERVAL_START;

  var schedule = integerInRange(value.schedule, 0, 2)
    ? value.schedule
    : DEFAULT_MEDICATION.schedule;

  var day = 0;

  if (time === 4) {
    schedule = 0;
  } else if (schedule === 1) {
    day = integerInRange(value.day, 0, 6)
      ? value.day
      : 0;
  } else if (schedule === 2) {
    day = integerInRange(value.day, 1, 31)
      ? value.day
      : 1;
  }

  var symbolValid = integerInRange(
    value.symbol,
    0,
    1
  );

  var symbol = symbolValid
    ? value.symbol
    : -1;

  var shapeValid = integerInRange(
    value.shape,
    0,
    4
  );

  var shape = shapeValid
    ? value.shape
    : -1;

  var colorValid = integerInRange(
    value.color,
    192,
    255
  );

  var color = colorValid
    ? value.color
    : -1;

  var color2Valid = integerInRange(
    value.color2,
    192,
    255
  );

  var color2 = color2Valid
    ? value.color2
    : -1;

  /*
   * Pens reuse the existing two Pebble colour slots:
   * primary colour = pen body, secondary colour = accent.
   * Old pen entries did not require colours, so migrate them in memory.
   */
  if (symbol === 1) {
    if (!colorValid) {
      color = 255;
      colorValid = true;
    }

    if (!color2Valid) {
      color2 = 240;
      color2Valid = true;
    }
  }

  var size = integerInRange(
    value.size,
    60,
    140
  )
    ? value.size
    : DEFAULT_MEDICATION.size;

  var imprint = typeof value.imprint === 'string'
    ? value.imprint.trim().slice(0, 5)
    : '';

  var iconSet =
      symbolValid &&
      (
        (
          symbol === 1 &&
          colorValid &&
          color2Valid
        ) ||
        (
          symbol === 0 &&
          shapeValid &&
          colorValid &&
          (shape !== 4 || color2Valid)
        )
      );

  return {
    name: name,
    dosage: dosage,
    effect: effect,
    quantity: quantity,
    time: time,
    schedule: schedule,
    day: day,
    symbol: symbol,
    shape: shape,
    color: color,
    color2: color2,
    size: size,
    imprint: imprint,
    iconSet: iconSet,
    enabled: value.enabled !== false && iconSet,
    intervalHours: intervalHours,
    intervalStart: intervalStart
  };
}

function normalizeMedications(values) {
  if (!Array.isArray(values)) {
    return [cloneDefaultMedication()];
  }

  var result = [];

  for (
    var index = 0;
    index < values.length &&
        result.length < MAX_MEDICATIONS;
    index++
  ) {
    result.push(
      normalizeMedication(values[index])
    );
  }

  return result;
}

function currentMedications() {
  var storedList = localStorage.getItem(
    MEDICATIONS_STORAGE_KEY
  );

  if (storedList) {
    try {
      return normalizeMedications(
        JSON.parse(storedList)
      );
    } catch (error) {
      console.log(
        'Could not read medication list: ' +
        error.message
      );
    }
  }

  var legacy = localStorage.getItem(
    LEGACY_MEDICATION_STORAGE_KEY
  );

  if (legacy) {
    try {
      return [
        normalizeMedication(
          JSON.parse(legacy)
        )
      ];
    } catch (error) {
      console.log(
        'Could not migrate medication: ' +
        error.message
      );
    }
  }

  return [cloneDefaultMedication()];
}

function sendMessage(message, next) {
  Pebble.sendAppMessage(
    message,
    function() {
      if (next) {
        next();
      }
    },
    function(error) {
      console.log(
        'Settings message failed: ' +
        JSON.stringify(error)
      );
    }
  );
}

function sendMedicationList(
    medications,
    commitSettings
) {
  var reset = {};
  reset[MED_COMMAND_KEY] = COMMAND_RESET;
  reset[MED_COUNT_KEY] = medications.length;

  sendMessage(reset, function() {
    sendMedicationAt(
      medications,
      0,
      commitSettings
    );
  });
}

function sendMedicationAt(
    medications,
    index,
    commitSettings
) {
  if (index >= medications.length) {
    var commit = commitSettings;
    commit[MED_COMMAND_KEY] = COMMAND_COMMIT;
    commit[MED_COUNT_KEY] = medications.length;
    sendMessage(commit);
    return;
  }

  var medication = medications[index];
  var message = {};
  var iconSet = medication.iconSet === true;
  var shape = iconSet && integerInRange(medication.shape, 0, 4)
      ? medication.shape
      : DEFAULT_MEDICATION.shape;
  var primaryColor = iconSet && integerInRange(medication.color, 192, 255)
      ? medication.color
      : DEFAULT_MEDICATION.color;
  var secondaryColor = iconSet && integerInRange(medication.color2, 192, 255)
      ? medication.color2
      : (medication.symbol === 1 ? 240 : primaryColor);
  var size = iconSet && integerInRange(medication.size, 60, 140)
      ? medication.size
      : 100;
  var imprint = iconSet && typeof medication.imprint === 'string'
      ? medication.imprint.trim().slice(0, 5)
      : '';

  message[MED_COMMAND_KEY] = COMMAND_ITEM;
  message[MED_INDEX_KEY] = index;
  message[MED_COUNT_KEY] = medications.length;
  message[MED_NAME_KEY] = medication.name;
  message[MED_DOSAGE_KEY] = medication.dosage || '';
  message[MED_EFFECT_KEY] = medication.effect || '';
  message[MED_QUANTITY_KEY] = medication.quantity;
  message[MED_TIME_KEY] = medication.time;
  message[MED_INTERVAL_HOURS_KEY] = medication.intervalHours;
  message[MED_INTERVAL_START_HOUR_KEY] =
      Math.floor(medication.intervalStart / 60);
  message[MED_INTERVAL_START_MINUTE_KEY] =
      medication.intervalStart % 60;
  message[MED_SCHEDULE_KEY] = medication.schedule;
  message[MED_DAY_KEY] = medication.day;
  message[MED_SYMBOL_KEY] = iconSet &&
      integerInRange(medication.symbol, 0, 1)
      ? medication.symbol
      : 0;
  message[MED_SHAPE_KEY] = shape;
  message[MED_COLOR_KEY] = primaryColor;
  message[MED_COLOR2_KEY] = secondaryColor;
  message[MED_SIZE_KEY] = size;
  message[MED_IMPRINT_KEY] = imprint;
  message[MED_ICON_SET_KEY] = iconSet ? 1 : 0;
  message[MED_ENABLED_KEY] =
      medication.enabled && iconSet ? 1 : 0;

  sendMessage(message, function() {
    sendMedicationAt(
      medications,
      index + 1,
      commitSettings
    );
  });
}

function sendAllSettings(
    theme,
    language,
    dayparts,
    medications,
    alarm,
    display
) {
  var transaction = Math.floor(
    Date.now() % 2147483647
  );

  if (transaction <= 0) {
    transaction = 1;
  }

  localStorage.setItem(
    SETTINGS_TRANSACTION_STORAGE_KEY,
    String(transaction)
  );

  var commitSettings = {};
  commitSettings[SETTINGS_TRANSACTION_KEY] = transaction;

  commitSettings[THEME_KEY] =
      theme === 'light' ? 1 : 0;
  commitSettings[LANGUAGE_KEY] =
      language === 'en' ? 1 : 0;
  commitSettings[SHOW_JAPANESE_PATTERN_KEY] =
      display.showPattern ? 1 : 0;
  commitSettings[DAYPART_MORNING_KEY] =
      dayparts.morning;
  commitSettings[DAYPART_NOON_KEY] =
      dayparts.noon;
  commitSettings[DAYPART_EVENING_KEY] =
      dayparts.evening;
  commitSettings[DAYPART_NIGHT_KEY] =
      dayparts.night;
  commitSettings[AUDIO_VOLUME_KEY] =
      alarm.audioEnabled
          ? alarm.audioVolume
          : 0;
  commitSettings[VIBRATION_ENABLED_KEY] =
      alarm.vibrationEnabled ? 1 : 0;
  commitSettings[REMINDER_INTERVAL_KEY] =
      alarm.reminderInterval;

  sendMedicationList(
    medications,
    commitSettings
  );
}

function safeJsonForScript(value) {
  return JSON.stringify(value)
    .replace(/</g, '\\u003c')
    .replace(/>/g, '\\u003e')
    .replace(/&/g, '\\u0026')
    .replace(/\u2028/g, '\\u2028')
    .replace(/\u2029/g, '\\u2029');
}

function minutesToTime(value) {
  var hours = Math.floor(value / 60);
  var minutes = value % 60;

  return (
    (hours < 10 ? '0' : '') +
    hours +
    ':' +
    (minutes < 10 ? '0' : '') +
    minutes
  );
}

function timeToMinutes(value) {
  if (
    typeof value !== 'string' ||
    !/^\d{2}:\d{2}$/.test(value)
  ) {
    return -1;
  }

  var parts = value.split(':');
  var hours = parseInt(parts[0], 10);
  var minutes = parseInt(parts[1], 10);

  if (
    hours < 0 ||
    hours > 23 ||
    minutes < 0 ||
    minutes > 59
  ) {
    return -1;
  }

  return hours * 60 + minutes;
}

function configurationPage(
    theme,
    language,
    dayparts,
    medications,
    alarm,
    display
) {
  var initialDayparts =
      safeJsonForScript(dayparts);
  var initialMedications =
      safeJsonForScript(medications);

  var lightSelected =
      theme === 'light' ? ' selected' : '';
  var darkSelected =
      theme === 'dark' ? ' selected' : '';
  var germanSelected =
      language === 'de' ? ' selected' : '';
  var englishSelected =
      language === 'en' ? ' selected' : '';
  var bodyClass =
      theme === 'light' ? '' : ' class="dark"';
  var alarmSettings = normalizeAlarmSettings(alarm);
  var alarmVolume = alarmSettings.audioVolume;
  var audioEnabledChecked =
      alarmSettings.audioEnabled ? ' checked' : '';
  var audioControlsClass =
      alarmSettings.audioEnabled
          ? 'alarm-volume-controls'
          : 'alarm-volume-controls hidden';
  var vibrationChecked =
      alarmSettings.vibrationEnabled ? ' checked' : '';
  var displaySettings =
      normalizeDisplaySettings(display);
  var patternChecked =
      displaySettings.showPattern ? ' checked' : '';
  var reminderIntervalSlider =
      reminderIntervalIndex(
        alarmSettings.reminderInterval
      );

  return [
    '<!doctype html>',
    '<html>',
    '<head>',
    '<meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width,initial-scale=1">',
    '<title>ナース (Nāsu)</title>',
    '<style>',
    'body{margin:0;background:#f2f2f2;color:#111;font-family:sans-serif}',
    'main{max-width:520px;margin:auto;padding:20px 14px 36px}',
    'h1{font-size:24px;margin:0 0 18px}',
    'section,.card{background:#fff;border-radius:10px;margin-bottom:13px}',
    '.plain{padding:15px}',
    '.theme-row{display:flex;align-items:center;justify-content:space-between;gap:14px;padding:11px 15px}',
    '.theme-row h2{margin:0;font-size:17px}',
    '.theme-row select{box-sizing:border-box;width:auto;min-width:104px;margin:0;padding:7px 30px 7px 10px;font-size:15px}',
    '.alarm-volume-label{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:0}',
    '.alarm-volume-value{font-weight:normal;font-variant-numeric:tabular-nums}',
    '.alarm-check{margin-top:16px}',
    'h2{font-size:18px;margin:0 0 13px}',
    'h3{font-size:17px;margin:0}',
    '.toggle{box-sizing:border-box;width:100%;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:15px;border:0;background:transparent;color:#111;text-align:left;font-size:17px;font-weight:bold}',
    '.card-header{display:flex;align-items:stretch}',
    '.card-header .toggle{flex:1;width:auto;min-width:0}',
    '.drag-handle{box-sizing:border-box;width:46px;flex:0 0 46px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;border:0;background:transparent;color:inherit;touch-action:none;user-select:none;cursor:grab}',
    '.drag-handle span{display:block;width:20px;height:2px;border-radius:2px;background:currentColor;opacity:.62}',
    '.card.dragging{opacity:.72}',
    '.card.dragging .drag-handle{cursor:grabbing}',
    '.card.dragging{position:relative;z-index:3;opacity:.78;box-shadow:0 5px 16px rgba(0,0,0,.28)}',
    '.summary-main{display:flex;align-items:center;gap:8px;min-width:0}',
    '.summary-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}',
    '.summary-icon{display:inline-block;box-sizing:border-box;flex:0 0 auto;border:1px solid #555;background-clip:padding-box}',
    '.summary-shape-0{width:14px;height:14px;border-radius:50%}',
    '.summary-shape-1{width:18px;height:11px;border-radius:50%}',
    '.summary-shape-2{width:20px;height:10px;border-radius:6px}',
    '.summary-shape-3{width:12px;height:12px;border-radius:2px;transform:rotate(45deg)}',
    '.summary-shape-4{width:22px;height:9px;border-radius:5px}',
    '.summary-pen{position:relative;width:19px;height:7px;border-radius:2px;background:#888;transform:rotate(-30deg)}',
    '.summary-pen:before{content:"";position:absolute;right:-6px;top:2px;width:6px;height:1px;background:#555}',
    '.summary-pen:after{content:"";position:absolute;left:-4px;top:-2px;width:3px;height:9px;border-radius:1px;background:#555}',
    '.summary-sub{display:block;margin-top:3px;color:#666;font-size:13px;font-weight:normal}',
    '.arrow{font-size:24px;line-height:1;transform:rotate(90deg)}',
    '.collapsed .arrow{transform:none}',
    '.body{padding:0 15px 15px}',
    '.hidden{display:none}',
    'label{display:block;font-size:14px;font-weight:bold;margin-top:13px}',
    'input[type=text],input[type=number],input[type=time],select{box-sizing:border-box;width:100%;margin-top:6px;padding:10px;font-size:16px}',
    '.check{display:flex;align-items:center;gap:10px}',
    '.check input{width:22px;height:22px}',
    '.card-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:16px}',
    '.remove,.copy{width:100%;border:0;border-radius:7px;padding:10px 7px;font-size:14px;font-weight:bold}',
    '.remove{background:#b00020;color:#fff}',
    '.copy{background:#eee;color:#111}',
    '.copy:disabled{opacity:.45}',
    '.close-medication{grid-column:1/-1;width:100%;border:0;border-radius:7px;padding:12px;font-size:15px;font-weight:bold;background:#ddd;color:#111}',
    '.add{display:block;width:auto;margin:2px auto 12px;padding:0 14px;border:0;background:transparent;color:inherit;font-size:36px;line-height:1.1;font-weight:bold}',
    '.save{width:100%;padding:13px;border:0;border-radius:8px;background:#111;color:#fff;font-size:17px;font-weight:bold}',
    '.empty{text-align:center;color:#666;padding:18px 6px}',
    '.note{color:#666;font-size:13px;line-height:1.35;margin-top:12px}',
    '.icon-fields{margin-top:14px;padding:12px;border:1px solid #d2d2d2;border-radius:9px}',
    '.icon-preview-row{display:flex;align-items:center;justify-content:center;margin-bottom:6px}',
    '.size-label{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:0}',
    '.size-value{font-weight:normal;font-variant-numeric:tabular-nums}',
    'input[type=range]{box-sizing:border-box;width:100%;margin-top:9px}',
    '.pill-preview{width:92px;height:76px;display:flex;align-items:center;justify-content:center}',
    '.pill-shape{display:flex;align-items:center;justify-content:center;border:2px solid #555;background-clip:padding-box;box-shadow:none}',
    '.pill-imprint{display:block;max-width:88%;overflow:hidden;font-size:10px;font-weight:bold;line-height:1;white-space:nowrap;text-align:center}',
    '.pen-preview-row{height:96px;display:flex;align-items:center;justify-content:center;margin-bottom:4px}',
    '.pen-preview-row.hidden{display:none}',
    '.pen-preview{position:relative;display:block;box-sizing:border-box;width:20px;height:72px;border:3px solid #555;border-radius:9px;background:#fff}',
    '.pen-preview:before{content:"";position:absolute;left:6px;bottom:-22px;width:2px;height:19px;background:#555}',
    '.pen-preview:after{content:"";position:absolute;left:3px;top:-10px;width:8px;height:5px;border-radius:2px;background:#555}',
    '.pen-preview-accent{position:absolute;left:2px;right:2px;bottom:5px;height:27px;border-radius:4px;background:#f00}',
    '.pen-preview-window{position:absolute;left:6px;top:17px;width:3px;height:14px;border-radius:2px;background:#555}',
    '.imprint-input{text-align:center;letter-spacing:.08em}',
    '.shape-0{width:28px;height:28px;border-radius:50%}',
    '.shape-1{width:39px;height:24px;border-radius:50%}',
    '.shape-2{width:42px;height:21px;border-radius:12px}',
    '.shape-3{width:27px;height:27px;border-radius:4px;transform:rotate(45deg)}',
    '.shape-4{width:46px;height:16px;border-radius:9px}',
    '.color-picker-row{display:flex;gap:16px;margin-top:13px}',
    '.color-picker{display:flex;flex-direction:column;align-items:center;gap:6px;font-size:13px;font-weight:bold}',
    '.color-picker.hidden{display:none}',
    '.color-tile{width:44px;height:34px;border:2px solid #555;border-radius:6px;padding:0;box-shadow:none}',
    '.palette-panel{margin-top:8px;padding-top:2px}',
    '.color-grid{display:grid;grid-template-columns:repeat(8,1fr);gap:6px;margin-top:8px}',
    '.color-swatch{aspect-ratio:1;border:2px solid rgba(0,0,0,.28);border-radius:5px;padding:0;min-width:0}',
    '.color-swatch.selected{outline:3px solid #111;outline-offset:1px}',
    '.icon-warning{color:#b00020;font-weight:bold}',
    '.check.disabled{opacity:.55}',
    'body.dark{background:#111;color:#f4f4f4;color-scheme:dark}',
    'body.dark section,body.dark .card{background:#202020}',
    'body.dark .toggle{color:#f4f4f4}',
    'body.dark .summary-sub,body.dark .empty,body.dark .note{color:#aaa}',
    'body.dark input[type=text],body.dark input[type=number],body.dark input[type=time],body.dark select{background:#2b2b2b;color:#fff;border:1px solid #555}',
    'body.dark .copy{background:#333;color:#f4f4f4}',
    'body.dark .close-medication{background:#444;color:#f4f4f4}',
    'body.dark .summary-icon{border-color:#aaa}',
    'body.dark .summary-pen:before,body.dark .summary-pen:after{background:#aaa}',
    'body.dark .color-tile{border-color:#aaa}',
    'body.dark .add{background:transparent;color:#f4f4f4}',
    'body.dark .save{background:#f2f2f2;color:#111}',
    'body.dark .icon-fields{border-color:#505050}',
    'body.dark .color-swatch.selected{outline-color:#fff}',
    'body.dark .icon-warning{color:#ff8a9a}',
    '</style>',
    '</head>',
    '<body' + bodyClass + '>',
    '<main>',
    '<h1>ナース <span style="font-size:.62em;font-weight:normal">Nāsu</span></h1>',
    '<form id="settings">',
    '<section id="medication-panel">',
    '<button id="medication-toggle" class="toggle" type="button">',
    '<span><span class="summary-main">Medikamente</span>',
    '<span class="summary-sub">Hinzufügen, bearbeiten und deaktivieren</span></span>',
    '<span class="arrow">›</span>',
    '</button>',
    '<div id="medication-body" class="body">',
    '<div id="medications"></div>',
    '<button id="add" class="add" type="button">+</button>',
    '</div>',
    '</section>',
    '<section id="daypart-panel" class="collapsed">',
    '<button id="daypart-toggle" class="toggle" type="button">',
    '<span><span class="summary-main">Tageszeiten</span>',
    '<span class="summary-sub">Früh, Mittag, Abend und Nacht</span></span>',
    '<span class="arrow">›</span>',
    '</button>',
    '<div id="daypart-body" class="body hidden">',
    '<label>Früh beginnt',
    '<input id="morning" type="time" required value="' + minutesToTime(dayparts.morning) + '">',
    '</label>',
    '<label>Mittag beginnt',
    '<input id="noon" type="time" required value="' + minutesToTime(dayparts.noon) + '">',
    '</label>',
    '<label>Abend beginnt',
    '<input id="evening" type="time" required value="' + minutesToTime(dayparts.evening) + '">',
    '</label>',
    '<label>Nacht beginnt',
    '<input id="night" type="time" required value="' + minutesToTime(dayparts.night) + '">',
    '</label>',
    '</div>',
    '</section>',
    '<section id="alarm-panel" class="collapsed">',
    '<button id="alarm-toggle" class="toggle" type="button">',
    '<span><span class="summary-main">Alarm</span>',
    '<span class="summary-sub">Ton, Vibration und Erinnerung</span></span>',
    '<span class="arrow">›</span>',
    '</button>',
    '<div id="alarm-body" class="body hidden">',
    '<label class="check alarm-check"><input id="vibration-enabled" type="checkbox"' + vibrationChecked + '><span>Vibration</span></label>',
    '<label class="check alarm-check"><input id="audio-enabled" type="checkbox"' + audioEnabledChecked + '><span>Alarmsound</span></label>',
    '<div id="audio-volume-controls" class="' + audioControlsClass + '">',
    '<label class="alarm-volume-label"><span>Lautstärke</span>',
    '<span id="audio-volume-value" class="alarm-volume-value">' + alarmVolume + ' %</span></label>',
    '<input id="audio-volume" type="range" min="1" max="100" step="1" value="' + alarmVolume + '">',
    '</div>',
    '<label class="alarm-volume-label"><span>Erneut erinnern</span>',
    '<span id="reminder-interval-value" class="alarm-volume-value">' + alarmSettings.reminderInterval + ' min</span></label>',
    '<input id="reminder-interval" type="range" min="0" max="6" step="1" value="' + reminderIntervalSlider + '">',
    '</div>',
    '</section>',
    '<section id="display-panel" class="collapsed">',
    '<button id="display-toggle" class="toggle" type="button">',
    '<span><span class="summary-main">Darstellung</span>',
    '<span class="summary-sub">Theme, Sprache und Details</span></span>',
    '<span class="arrow">›</span>',
    '</button>',
    '<div id="display-body" class="body hidden">',
    '<label>Theme',
    '<select id="theme" aria-label="Theme">',
    '<option value="light"' + lightSelected + '>Hell</option>',
    '<option value="dark"' + darkSelected + '>Dunkel</option>',
    '</select>',
    '</label>',
    '<label>Sprache',
    '<select id="language" aria-label="Language">',
    '<option value="de"' + germanSelected + '>Deutsch</option>',
    '<option value="en"' + englishSelected + '>English</option>',
    '</select>',
    '</label>',
    '<label class="check alarm-check"><input id="show-pattern" type="checkbox"' + patternChecked + '><span>Japanisches Muster</span></label>',
    '</div>',
    '</section>',
    '<button class="save" type="submit">Übertragen</button>',
    '</form>',
    '</main>',
    '<script>',
    'var MAX_MEDICATIONS=' + MAX_MEDICATIONS + ';',
    'var dayparts=' + initialDayparts + ';',
    'var medications=' + initialMedications + ';',
    'var language="' + language + '";',
    'function tr(de,en){return language==="en"?en:de;}',
    'var translations={"Medikamente":"Medications","Hinzufügen, bearbeiten und deaktivieren":"Add, edit and disable","Tageszeiten":"Dayparts","Früh, Mittag, Abend und Nacht":"Morning, noon, evening and night","Früh beginnt":"Morning starts","Mittag beginnt":"Noon starts","Abend beginnt":"Evening starts","Nacht beginnt":"Night starts","Ton, Vibration und Erinnerung":"Sound, vibration and reminders","Alarmsound":"Alarm sound","Lautstärke":"Volume","Erneut erinnern":"Remind again","Darstellung":"Appearance","Theme, Sprache und Details":"Theme, language and details","Wappen und Hintergrundmuster":"Emblem and background pattern","Schweizer Wappen":"Swiss emblem","Japanisches Muster":"Japanese pattern","Hell":"Light","Dunkel":"Dark","Sprache":"Language","Übertragen":"Save to watch","Noch kein Medikament angelegt.":"No medication added yet.","Neues Medikament":"New medication","Medikament verschieben":"Move medication","Wirkung":"Effect","z. B. Blutverdünner":"e.g. blood thinner","Dosierung":"Dosage","z. B. 20 mg":"e.g. 20 mg","Menge":"Quantity","Zeitpunkt":"Time","Früh":"Morning","Mittag":"Noon","Abend":"Evening","Nacht":"Night","Intervall":"Interval","Wiederholung":"Repeat","Startzeit":"Start time","Alle 2 Stunden":"Every 2 hours","Alle 3 Stunden":"Every 3 hours","Alle 4 Stunden":"Every 4 hours","Alle 6 Stunden":"Every 6 hours","Alle 8 Stunden":"Every 8 hours","Alle 12 Stunden":"Every 12 hours","Rhythmus":"Schedule","Täglich":"Daily","Wöchentlich":"Weekly","Monatlich":"Monthly","Wochentag":"Weekday","Montag":"Monday","Dienstag":"Tuesday","Mittwoch":"Wednesday","Donnerstag":"Thursday","Freitag":"Friday","Samstag":"Saturday","Sonntag":"Sunday","Tag im Monat":"Day of month","Art":"Type","Bitte auswählen":"Please select","Tablette":"Tablet","Pen / Spritze":"Pen / syringe","Form":"Shape","Rund":"Round","Pille":"Pill","Kapsel":"Capsule","Rhombus":"Diamond","Grösse":"Size","Beschriftung":"Imprint","z. B. 20":"e.g. 20","Farbe":"Color","Farbe 1":"Color 1","Farbe 2":"Color 2","Pen-Farbe":"Pen color","Akzent":"Accent","Farbpalette öffnen":"Open color palette","Aktiv":"Active","Bitte zuerst ein vollständiges Icon auswählen. Erst danach kann das Medikament aktiviert werden.":"Please select a complete icon first. Only then can the medication be activated.","Medikament löschen":"Delete medication","Medikament kopieren":"Copy medication","Schliessen":"Close"};',
    'function translateNode(node){',
    'if(language!=="en"||!node){return;}',
    'if(node.nodeType===3){var raw=node.nodeValue;var trimmed=raw.replace(/^\\s+|\\s+$/g,"");if(translations[trimmed]){node.nodeValue=raw.replace(trimmed,translations[trimmed]);}return;}',
    'if(node.nodeType!==1){return;}',
    'var attrs=["placeholder","aria-label","title"];',
    'for(var attributeIndex=0;attributeIndex<attrs.length;attributeIndex++){var attr=attrs[attributeIndex];var value=node.getAttribute&&node.getAttribute(attr);if(value&&translations[value]){node.setAttribute(attr,translations[value]);}}',
    'for(var childIndex=0;childIndex<node.childNodes.length;childIndex++){translateNode(node.childNodes[childIndex]);}',
    '}',
    'function translatePage(){translateNode(document.body);}',
    'var timeNames=language==="en"?["Morning","Noon","Evening","Night","Interval"]:["Früh","Mittag","Abend","Nacht","Intervall"];',
    'var reminderIntervals=[1,5,10,15,20,30,60];',
    'function escapeHtml(value){',
    'return String(value).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");',
    '}',
    'function option(value,label,current){',
    'return "<option value=\\""+value+"\\""+(value===current?" selected":"")+">"+label+"</option>";',
    '}',
    'function blankMedication(){',
    'return {name:"",dosage:"",effect:"",quantity:1,time:0,schedule:0,day:0,symbol:-1,shape:-1,color:-1,color2:-1,size:100,imprint:"",iconSet:false,enabled:false,intervalHours:4,intervalStart:480};',
    '}',
    'function numberValue(card,name){',
    'return parseInt(card.querySelector("[data-field=\\""+name+"\\"]").value,10);',
    '}',
    'function readMedications(){',
    'var cards=document.querySelectorAll(".card");',
    'var result=[];',
    'for(var i=0;i<cards.length;i++){',
    'var card=cards[i];',
    'var dosage=card.querySelector("[data-field=\\"dosage\\"]").value.trim().slice(0,20);',
    'var effect=card.querySelector("[data-field=\\"effect\\"]").value.trim().slice(0,31);',
    'var time=numberValue(card,"time");',
    'var schedule=time===4?0:numberValue(card,"schedule");',
    'var day=time===4?0:(schedule===1?numberValue(card,"weekday"):(schedule===2?numberValue(card,"monthday"):0));',
    'var intervalHours=numberValue(card,"intervalHours");',
    'var intervalStart=window.__timeToMinutes(card.querySelector("[data-field=intervalStart]").value);',
    'if(intervalStart<0){intervalStart=480;}',
    'var symbol=numberValue(card,"symbol");',
    'var shape=numberValue(card,"shape");',
    'var color=numberValue(card,"color");',
    'var color2=numberValue(card,"color2");',
    'var size=numberValue(card,"size");',
    'var imprint=card.querySelector("[data-field=\\"imprint\\"]").value.trim().slice(0,5);',
    'var iconSet=symbol===1||(symbol===0&&shape>=0&&shape<=4&&color>=192&&color<=255&&(shape!==4||(color2>=192&&color2<=255)));',
    'var enabled=card.querySelector("[data-field=\\"enabled\\"]").checked&&iconSet;',
    'result.push({',
    'name:card.querySelector("[data-field=\\"name\\"]").value.trim(),',
    'dosage:dosage,',
    'effect:effect,',
    'quantity:numberValue(card,"quantity"),',
    'time:time,',
    'schedule:schedule,',
    'day:day,',
    'symbol:symbol,',
    'shape:shape,',
    'color:color,',
    'color2:color2,',
    'size:size,',
    'imprint:imprint,',
    'iconSet:iconSet,',
    'enabled:enabled,',
    'intervalHours:intervalHours,',
    'intervalStart:intervalStart',
    '});',
    '}',
    'return result;',
    '}',
    'function updateDayFields(card){',
    'var time=numberValue(card,"time");',
    'var interval=time===4;',
    'var schedule=numberValue(card,"schedule");',
    'card.querySelector(".schedule-field").className=interval?"schedule-field hidden":"schedule-field";',
    'card.querySelector(".interval-hours").className=interval?"interval-hours":"interval-hours hidden";',
    'card.querySelector(".interval-start").className=interval?"interval-start":"interval-start hidden";',
    'card.querySelector(".weekday").className=!interval&&schedule===1?"weekday":"weekday hidden";',
    'card.querySelector(".monthday").className=!interval&&schedule===2?"monthday":"monthday hidden";',
    '}',
    'function pebbleColorHex(value){',
    'var red=((value>>4)&3)*85;',
    'var green=((value>>2)&3)*85;',
    'var blue=(value&3)*85;',
    'function hex(component){var result=component.toString(16);return result.length<2?"0"+result:result;}',
    'return "#"+hex(red)+hex(green)+hex(blue);',
    '}',
    'function pebbleTextColor(value){',
    'var red=((value>>4)&3)*85;',
    'var green=((value>>2)&3)*85;',
    'var blue=(value&3)*85;',
    'return red*299+green*587+blue*114>145000?"#111":"#fff";',
    '}',
    'function paletteHtml(current,field){',
    'var html="";',
    'for(var value=192;value<=255;value++){',
    'var selected=value===current?" selected":"";',
    'html+="<button class=\\"color-swatch"+selected+"\\" type=\\"button\\" data-color=\\""+value+"\\" data-color-field=\\""+field+"\\" title=\\"Pebble ARGB8 0x"+value.toString(16).toUpperCase()+"\\" style=\\"background:"+pebbleColorHex(value)+"\\"></button>";',
    '}',
    'return html;',
    '}',
    'function updateIconFields(card){',
    'var fields=card.querySelector(".icon-fields");',
    'var symbol=numberValue(card,"symbol");',
    'var isTablet=symbol===0;',
    'var isPen=symbol===1;',
    'var shape=numberValue(card,"shape");',
    'var isCapsule=shape===4;',
    'var color=numberValue(card,"color");',
    'var color2=numberValue(card,"color2");',
    'if(isPen&&!(color>=192&&color<=255)){color=255;card.querySelector("[data-field=\\"color\\"]").value=color;}',
    'if(isPen&&!(color2>=192&&color2<=255)){color2=240;card.querySelector("[data-field=\\"color2\\"]").value=color2;}',
    'var colorValid=color>=192&&color<=255;',
    'var color2Valid=color2>=192&&color2<=255;',
    'var size=numberValue(card,"size");',
    'var imprint=card.querySelector("[data-field=\\"imprint\\"]").value.trim().slice(0,5);',
    'if(size<60||size>140){size=100;}',
    'var iconSet=(isPen&&colorValid&&color2Valid)||(isTablet&&shape>=0&&shape<=4&&colorValid&&(!isCapsule||color2Valid));',
    'fields.className=(isTablet||isPen)?"icon-fields":"icon-fields hidden";',
    'card.querySelector(".tablet-fields").className=isTablet?"tablet-fields":"tablet-fields hidden";',
    'card.querySelector(".pen-preview-row").className=isPen?"pen-preview-row":"pen-preview-row hidden";',
    'if(isTablet){',
    'var preview=card.querySelector(".pill-shape");',
    'preview.className=shape>=0&&shape<=4?"pill-shape shape-"+shape:"pill-shape hidden";',
    'if(isCapsule){',
    'preview.style.backgroundColor="transparent";',
    'preview.style.backgroundImage=colorValid&&color2Valid?"linear-gradient(90deg,"+pebbleColorHex(color)+" 0 50%,"+pebbleColorHex(color2)+" 50% 100%)":"none";',
    '}else{',
    'preview.style.backgroundImage="none";',
    'preview.style.backgroundColor=colorValid?pebbleColorHex(color):"transparent";',
    '}',
    'preview.style.transform=(shape===3?"rotate(45deg) ":"")+"scale("+(size/100)+")";',
    'var imprintNode=card.querySelector(".pill-imprint");',
    'imprintNode.textContent=imprint;',
    'imprintNode.style.color=colorValid?pebbleTextColor(color):"#111";',
    'card.querySelector("[data-size-value]").textContent=size+" %";',
    '}',
    'if(isPen){',
    'card.querySelector(".pen-preview").style.backgroundColor=pebbleColorHex(color);',
    'card.querySelector(".pen-preview-accent").style.backgroundColor=pebbleColorHex(color2);',
    '}',
    'card.querySelector(".primary-color-label").textContent=isPen?tr("Pen-Farbe","Pen color"):(isCapsule?tr("Farbe 1","Color 1"):tr("Farbe","Color"));',
    'card.querySelector(".secondary-color-label").textContent=isPen?tr("Akzent","Accent"):tr("Farbe 2","Color 2");',
    'card.querySelector(".second-color-picker").className=(isPen||isCapsule)?"color-picker second-color-picker":"color-picker second-color-picker hidden";',
    'var primaryTile=card.querySelector("[data-palette-toggle=\\"color\\"]");',
    'primaryTile.style.backgroundColor=colorValid?pebbleColorHex(color):"transparent";',
    'var secondTile=card.querySelector("[data-palette-toggle=\\"color2\\"]");',
    'secondTile.style.backgroundColor=color2Valid?pebbleColorHex(color2):"transparent";',
    'if(!(isPen||isCapsule)){card.querySelector("[data-palette-field=\\"color2\\"]").className="palette-panel hidden";}',
    'var swatches=card.querySelectorAll("[data-color]");',
    'for(var i=0;i<swatches.length;i++){',
    'var field=swatches[i].getAttribute("data-color-field");',
    'var selected=parseInt(swatches[i].getAttribute("data-color"),10)===numberValue(card,field);',
    'swatches[i].className=selected?"color-swatch selected":"color-swatch";',
    '}',
    'var active=card.querySelector("[data-field=\\"enabled\\"]");',
    'if(!iconSet){active.checked=false;}',
    'active.disabled=!iconSet;',
    'active.parentNode.className=iconSet?"check":"check disabled";',
    'card.querySelector(".icon-warning").className=iconSet?"note icon-warning hidden":"note icon-warning";',
    '}',
    'function setCardOpen(card,open){',
    'var body=card.querySelector(".body");',
    'var toggle=card.querySelector(".toggle");',
    'body.className=open?"body":"body hidden";',
    'toggle.className=open?"toggle":"toggle collapsed";',
    'if(!open){',
    'var palettes=card.querySelectorAll("[data-palette-field]");',
    'for(var paletteIndex=0;paletteIndex<palettes.length;paletteIndex++){palettes[paletteIndex].className="palette-panel hidden";}',
    '}',
    '}',
    'function setExclusiveCardOpen(card,open){',
    'var cards=document.querySelectorAll(".card");',
    'for(var i=0;i<cards.length;i++){',
    'setCardOpen(cards[i],cards[i]===card&&open);',
    '}',
    '}',
    'function scrollCardToTop(card){',
    'var toggle=card.querySelector(".toggle");',
    'if(toggle&&typeof toggle.scrollIntoView==="function"){',
    'setTimeout(function(){toggle.scrollIntoView(true);},0);',
    '}',
    '}',
    'function medicationCardAtPoint(x,y){',
    'if(!document.elementFromPoint){return null;}',
    'var node=document.elementFromPoint(x,y);',
    'while(node&&node!==document.body){',
    'if(typeof node.className==="string"&&(" "+node.className+" ").indexOf(" card ")>=0){return node;}',
    'node=node.parentNode;',
    '}',
    'return null;',
    '}',
    'function bindMedicationDrag(handle,card){',
    'var state=null;',
    'function eventY(event){',
    'if(event.touches&&event.touches.length){return event.touches[0].clientY;}',
    'if(event.changedTouches&&event.changedTouches.length){return event.changedTouches[0].clientY;}',
    'return typeof event.clientY==="number"?event.clientY:null;',
    '}',
    'function addTouch(name,handler){',
    'try{document.addEventListener(name,handler,{passive:false});}',
    'catch(error){document.addEventListener(name,handler,false);}',
    '}',
    'function removeTouch(name,handler){',
    'try{document.removeEventListener(name,handler,{passive:false});}',
    'catch(error){document.removeEventListener(name,handler,false);}',
    '}',
    'function start(event){',
    'if(state){return;}',
    'if(event.button!==undefined&&event.button!==0){return;}',
    'if(event.preventDefault){event.preventDefault();}',
    'state={wasOpen:card.querySelector(".body").className.indexOf("hidden")<0};',
    'if(card.className.indexOf("dragging")<0){card.className+=" dragging";}',
    'document.addEventListener("mousemove",move,false);',
    'document.addEventListener("mouseup",finish,false);',
    'addTouch("touchmove",move);',
    'addTouch("touchend",finish);',
    'addTouch("touchcancel",finish);',
    '}',
    'function move(event){',
    'if(!state){return;}',
    'if(event.preventDefault){event.preventDefault();}',
    'var y=eventY(event);',
    'if(y===null){return;}',
    'if(window.scrollBy&&window.innerHeight){',
    'if(y<55){window.scrollBy(0,-14);}else if(y>window.innerHeight-55){window.scrollBy(0,14);}',
    '}',
    'var cards=document.querySelectorAll(".card");',
    'var placed=false;',
    'for(var index=0;index<cards.length;index++){',
    'var target=cards[index];',
    'if(target===card){continue;}',
    'var rect=target.getBoundingClientRect();',
    'if(y<rect.top+rect.height/2){target.parentNode.insertBefore(card,target);placed=true;break;}',
    '}',
    'if(!placed&&card.parentNode){card.parentNode.appendChild(card);}',
    '}',
    'function finish(event){',
    'if(!state){return;}',
    'if(event&&event.preventDefault){event.preventDefault();}',
    'document.removeEventListener("mousemove",move,false);',
    'document.removeEventListener("mouseup",finish,false);',
    'removeTouch("touchmove",move);',
    'removeTouch("touchend",finish);',
    'removeTouch("touchcancel",finish);',
    'card.className=card.className.replace(" dragging","");',
    'medications=readMedications();',
    'var cards=document.querySelectorAll(".card");',
    'var newIndex=-1;',
    'for(var index=0;index<cards.length;index++){if(cards[index]===card){newIndex=index;break;}}',
    'var reopen=state.wasOpen;',
    'state=null;',
    'render(reopen?newIndex:-1);',
    '}',
    'handle.onmousedown=start;',
    'handle.ontouchstart=start;',
    '}',
    'function setPanelOpen(panelId,bodyId,open){',
    'var panel=document.getElementById(panelId);',
    'var body=document.getElementById(bodyId);',
    'body.className=open?"body":"body hidden";',
    'panel.className=open?"":"collapsed";',
    '}',
    'function render(openIndex){',
    'var host=document.getElementById("medications");',
    'if(medications.length===0){host.innerHTML="<section class=\\"empty\\">Noch kein Medikament angelegt.</section>";}',
    'else{',
    'var html="";',
    'for(var i=0;i<medications.length;i++){',
    'var med=medications[i];',
    'var title=med.name||"Neues Medikament";',
    'var dosage=typeof med.dosage==="string"?med.dosage.slice(0,20):"";',
    'var effect=typeof med.effect==="string"?med.effect.slice(0,31):"";',
    'var iconReady=med.iconSet===true;',
    'var imprint=typeof med.imprint==="string"?med.imprint.slice(0,5):"";',
    'var previewClass=med.shape>=0&&med.shape<=4?"pill-shape shape-"+med.shape:"pill-shape hidden";',
    'var summaryIcon="";',
    'if(med.symbol===0&&med.shape>=0&&med.shape<=4&&med.color>=192&&med.color<=255){',
    'var summaryStyle="";',
    'if(med.shape===4&&med.color2>=192&&med.color2<=255){',
    'summaryStyle="background-image:linear-gradient(90deg,"+pebbleColorHex(med.color)+" 0 50%,"+pebbleColorHex(med.color2)+" 50% 100%)";',
    '}else{summaryStyle="background-color:"+pebbleColorHex(med.color);}',
    'summaryIcon="<span class=\\"summary-icon summary-shape-"+med.shape+"\\" style=\\""+summaryStyle+"\\"></span>";',
    '}else if(med.symbol===1){summaryIcon="<span class=\\"summary-icon summary-pen\\"></span>";}',
    'var timeSummary=med.time===4?timeNames[4]+" · "+med.intervalHours+" h":timeNames[med.time];',
    'var sub=timeSummary+(dosage?" · "+dosage:"")+(effect?" · "+effect:"")+(med.quantity>1?" · x"+med.quantity:"")+(med.enabled?"":" · "+tr("aus","off"));',
    'html+="<section class=\\"card\\" data-index=\\""+i+"\\">";',
    'html+="<div class=\\"card-header\\">";',
    'html+="<button class=\\"drag-handle\\" type=\\"button\\" data-drag=\\""+i+"\\" aria-label=\\"Medikament verschieben\\"><span></span><span></span><span></span></button>";',
    'html+="<button class=\\"toggle collapsed\\" type=\\"button\\" data-toggle=\\""+i+"\\">";',
    'html+="<span><span class=\\"summary-main\\"><span class=\\"summary-name\\">"+escapeHtml(title)+"</span>"+summaryIcon+"</span><span class=\\"summary-sub\\">"+escapeHtml(sub)+"</span></span><span class=\\"arrow\\">›</span></button></div>";',
    'html+="<div class=\\"body hidden\\">";',
    'html+="<label>Name<input data-field=\\"name\\" type=\\"text\\" required maxlength=\\"31\\" value=\\""+escapeHtml(med.name)+"\\"></label>";',
    'html+="<label>Wirkung<input data-field=\\"effect\\" type=\\"text\\" maxlength=\\"31\\" value=\\""+escapeHtml(effect)+"\\" placeholder=\\"z. B. Blutverdünner\\"></label>";',
    'html+="<label>Dosierung<input data-field=\\"dosage\\" type=\\"text\\" maxlength=\\"20\\" value=\\""+escapeHtml(dosage)+"\\" placeholder=\\"z. B. 20 mg\\"></label>";',
    'html+="<label>Menge<input data-field=\\"quantity\\" type=\\"number\\" min=\\"1\\" max=\\"20\\" required value=\\""+med.quantity+"\\"></label>";',
    'html+="<label>Zeitpunkt<select data-field=\\"time\\">";',
    'html+=option(0,"Früh",med.time)+option(1,"Mittag",med.time)+option(2,"Abend",med.time)+option(3,"Nacht",med.time)+option(4,"Intervall",med.time);',
    'html+="</select></label>";',
    'html+="<label class=schedule-field>Rhythmus<select data-field=schedule>";',
    'html+=option(0,"Täglich",med.schedule)+option(1,"Wöchentlich",med.schedule)+option(2,"Monatlich",med.schedule);',
    'html+="</select></label>";',
    'html+="<label class=interval-hours>Wiederholung<select data-field=intervalHours>";',
    'html+=option(2,"Alle 2 Stunden",med.intervalHours)+option(3,"Alle 3 Stunden",med.intervalHours)+option(4,"Alle 4 Stunden",med.intervalHours)+option(6,"Alle 6 Stunden",med.intervalHours)+option(8,"Alle 8 Stunden",med.intervalHours)+option(12,"Alle 12 Stunden",med.intervalHours);',
    'html+="</select></label>";',
    'html+="<label class=interval-start>Startzeit<input data-field=intervalStart type=time required value="+window.__minutesToTime(med.intervalStart)+"></label>";',
    'html+="<label class=\\"weekday\\">Wochentag<select data-field=\\"weekday\\">";',
    'html+=option(0,"Montag",med.day)+option(1,"Dienstag",med.day)+option(2,"Mittwoch",med.day)+option(3,"Donnerstag",med.day)+option(4,"Freitag",med.day)+option(5,"Samstag",med.day)+option(6,"Sonntag",med.day);',
    'html+="</select></label>";',
    'html+="<label class=\\"monthday\\">Tag im Monat<input data-field=\\"monthday\\" type=\\"number\\" min=\\"1\\" max=\\"31\\" required value=\\""+(med.schedule===2?med.day:1)+"\\"></label>";',
    'html+="<label>Art<select data-field=\\"symbol\\">";',
    'html+=option(-1,"Bitte auswählen",med.symbol)+option(0,"Tablette",med.symbol)+option(1,"Pen / Spritze",med.symbol);',
    'html+="</select></label>";',
    'html+="<div class=\\"icon-fields\\">";',
    'html+="<div class=\\"tablet-fields\\">";',
    'html+="<label>Form<select data-field=\\"shape\\">";',
    'html+=option(-1,"Bitte auswählen",med.shape)+option(0,"Rund",med.shape)+option(1,"Ellipse",med.shape)+option(2,"Pille",med.shape)+option(4,"Kapsel",med.shape)+option(3,"Rhombus",med.shape);',
    'html+="</select></label>";',
    'html+="<div class=\\"icon-preview-row\\"><span class=\\"pill-preview\\"><span class=\\""+previewClass+"\\"><span class=\\"pill-imprint\\">"+escapeHtml(imprint)+"</span></span></span></div>";',
    'html+="<label class=\\"size-label\\"><span>Grösse</span><span class=\\"size-value\\" data-size-value>"+med.size+" %</span></label>";',
    'html+="<input data-field=\\"size\\" type=\\"range\\" min=\\"60\\" max=\\"140\\" step=\\"5\\" value=\\""+med.size+"\\">";',
    'html+="<label>Beschriftung<input class=\\"imprint-input\\" data-field=\\"imprint\\" type=\\"text\\" maxlength=\\"5\\" value=\\""+escapeHtml(imprint)+"\\" placeholder=\\"z. B. 20\\"></label>";',
    'html+="</div>";',
    'html+="<div class=\\"pen-preview-row hidden\\"><span class=\\"pen-preview\\"><span class=\\"pen-preview-accent\\"></span><span class=\\"pen-preview-window\\"></span></span></div>";',
    'html+="<input data-field=\\"color\\" type=\\"hidden\\" value=\\""+med.color+"\\">";',
    'html+="<input data-field=\\"color2\\" type=\\"hidden\\" value=\\""+med.color2+"\\">";',
    'html+="<div class=\\"color-picker-row\\">";',
    'html+="<div class=\\"color-picker\\"><span class=\\"primary-color-label\\">Farbe</span><button class=\\"color-tile\\" type=\\"button\\" data-palette-toggle=\\"color\\" title=\\"Farbpalette öffnen\\"></button></div>";',
    'html+="<div class=\\"color-picker second-color-picker hidden\\"><span class=\\"secondary-color-label\\">Farbe 2</span><button class=\\"color-tile\\" type=\\"button\\" data-palette-toggle=\\"color2\\" title=\\"Farbpalette öffnen\\"></button></div>";',
    'html+="</div>";',
    'html+="<div class=\\"palette-panel hidden\\" data-palette-field=\\"color\\"><div class=\\"color-grid\\">"+paletteHtml(med.color,"color")+"</div></div>";',
    'html+="<div class=\\"palette-panel hidden\\" data-palette-field=\\"color2\\"><div class=\\"color-grid\\">"+paletteHtml(med.color2,"color2")+"</div></div>";',
    'html+="</div>";',
    'html+="<label class=\\""+(iconReady?"check":"check disabled")+"\\"><input data-field=\\"enabled\\" type=\\"checkbox\\""+(med.enabled?" checked":"")+(iconReady?"":" disabled")+"><span>Aktiv</span></label>";',
    'html+="<div class=\\""+(iconReady?"note icon-warning hidden":"note icon-warning")+"\\">Bitte zuerst ein vollständiges Icon auswählen. Erst danach kann das Medikament aktiviert werden.</div>";',
    'html+="<div class=\\"card-actions\\"><button class=\\"remove\\" type=\\"button\\" data-remove=\\""+i+"\\">Medikament löschen</button><button class=\\"copy\\" type=\\"button\\" data-copy=\\""+i+"\\""+(medications.length>=MAX_MEDICATIONS?" disabled":"")+">Medikament kopieren</button><button class=\\"close-medication\\" type=\\"button\\" data-close=\\""+i+"\\">Schliessen</button></div>";',
    'html+="</div></section>";',
    '}',
    'host.innerHTML=html;',
    '}',
    'var cards=document.querySelectorAll(".card");',
    'for(var c=0;c<cards.length;c++){',
    'var card=cards[c];',
    'updateDayFields(card);',
    'updateIconFields(card);',
    'card.querySelector("[data-field=time]").onchange=(function(item){return function(){updateDayFields(item);};})(card);',
    'card.querySelector("[data-field=\\"schedule\\"]").onchange=(function(item){return function(){updateDayFields(item);};})(card);',
    'card.querySelector("[data-field=\\"symbol\\"]").onchange=(function(item){return function(){updateIconFields(item);};})(card);',
    'card.querySelector("[data-field=\\"shape\\"]").onchange=(function(item){return function(){updateIconFields(item);};})(card);',
    'card.querySelector("[data-field=\\"size\\"]").oninput=(function(item){return function(){updateIconFields(item);};})(card);',
    'card.querySelector("[data-field=\\"imprint\\"]").oninput=(function(item){return function(){updateIconFields(item);};})(card);',
    'var paletteButtons=card.querySelectorAll("[data-palette-toggle]");',
    'for(var paletteButtonIndex=0;paletteButtonIndex<paletteButtons.length;paletteButtonIndex++){',
    'paletteButtons[paletteButtonIndex].onclick=(function(item){return function(){',
    'var field=this.getAttribute("data-palette-toggle");',
    'var target=item.querySelector("[data-palette-field=\\""+field+"\\"]");',
    'var shouldOpen=target.className.indexOf("hidden")>=0;',
    'var panels=item.querySelectorAll("[data-palette-field]");',
    'for(var panelIndex=0;panelIndex<panels.length;panelIndex++){panels[panelIndex].className="palette-panel hidden";}',
    'if(shouldOpen){target.className="palette-panel";}',
    '};})(card);',
    '}',
    'var colorButtons=card.querySelectorAll("[data-color]");',
    'for(var colorIndex=0;colorIndex<colorButtons.length;colorIndex++){',
    'colorButtons[colorIndex].onclick=(function(item){return function(){var field=this.getAttribute("data-color-field");item.querySelector("[data-field=\\""+field+"\\"]").value=this.getAttribute("data-color");updateIconFields(item);var panel=item.querySelector("[data-palette-field=\\""+field+"\\"]");if(panel){panel.className="palette-panel hidden";}};})(card);',
    '}',
    'var dragHandle=card.querySelector("[data-drag]");',
    'if(dragHandle){bindMedicationDrag(dragHandle,card);}',
    'card.querySelector("[data-toggle]").onclick=(function(item){return function(){var hidden=item.querySelector(".body").className.indexOf("hidden")>=0;setExclusiveCardOpen(item,hidden);if(hidden){scrollCardToTop(item);}};})(card);',
    'if(parseInt(card.getAttribute("data-index"),10)===openIndex){setCardOpen(card,true);}',
    '}',
    'var removeButtons=document.querySelectorAll("[data-remove]");',
    'for(var r=0;r<removeButtons.length;r++){',
    'removeButtons[r].onclick=function(){',
    'medications=readMedications();',
    'medications.splice(parseInt(this.getAttribute("data-remove"),10),1);',
    'render(-1);',
    '};',
    '}',
    'var copyButtons=document.querySelectorAll("[data-copy]");',
    'for(var copyIndex=0;copyIndex<copyButtons.length;copyIndex++){',
    'copyButtons[copyIndex].onclick=function(){',
    'medications=readMedications();',
    'if(medications.length>=MAX_MEDICATIONS){return;}',
    'var sourceIndex=parseInt(this.getAttribute("data-copy"),10);',
    'var copy=JSON.parse(JSON.stringify(medications[sourceIndex]));',
    'medications.splice(sourceIndex+1,0,copy);',
    'render(sourceIndex+1);',
    'var copiedCard=document.querySelector(".card[data-index=\\""+(sourceIndex+1)+"\\"]");',
    'if(copiedCard){scrollCardToTop(copiedCard);}',
    '};',
    '}',
    'var closeButtons=document.querySelectorAll("[data-close]");',
    'for(var closeIndex=0;closeIndex<closeButtons.length;closeIndex++){',
    'closeButtons[closeIndex].onclick=function(){',
    'var medicationIndex=parseInt(this.getAttribute("data-close"),10);',
    'medications=readMedications();',
    'render(-1);',
    'var closedCard=document.querySelector(".card[data-index=\\""+medicationIndex+"\\"]");',
    'if(closedCard){scrollCardToTop(closedCard);}',
    '};',
    '}',
    'document.getElementById("add").disabled=medications.length>=MAX_MEDICATIONS;',
    'translatePage();',
    '}',
    'var audioVolume=document.getElementById("audio-volume");',
    'audioVolume.oninput=function(){document.getElementById("audio-volume-value").textContent=this.value+" %";};',
    'var audioEnabled=document.getElementById("audio-enabled");',
    'function updateAudioControls(){document.getElementById("audio-volume-controls").className=audioEnabled.checked?"alarm-volume-controls":"alarm-volume-controls hidden";}',
    'audioEnabled.onchange=updateAudioControls;',
    'updateAudioControls();',
    'var reminderInterval=document.getElementById("reminder-interval");',
    'reminderInterval.oninput=function(){document.getElementById("reminder-interval-value").textContent=reminderIntervals[parseInt(this.value,10)]+" min";};',
    'document.getElementById("theme").onchange=function(){',
    'document.body.className=this.value==="dark"?"dark":"";',
    '};',
    'document.getElementById("daypart-toggle").onclick=function(){',
    'var body=document.getElementById("daypart-body");',
    'setPanelOpen("daypart-panel","daypart-body",body.className.indexOf("hidden")>=0);',
    '};',
    'document.getElementById("alarm-toggle").onclick=function(){',
    'var body=document.getElementById("alarm-body");',
    'setPanelOpen("alarm-panel","alarm-body",body.className.indexOf("hidden")>=0);',
    '};',
    'document.getElementById("display-toggle").onclick=function(){',
    'var body=document.getElementById("display-body");',
    'setPanelOpen("display-panel","display-body",body.className.indexOf("hidden")>=0);',
    '};',
    'document.getElementById("medication-toggle").onclick=function(){',
    'var body=document.getElementById("medication-body");',
    'setPanelOpen("medication-panel","medication-body",body.className.indexOf("hidden")>=0);',
    '};',
    'document.getElementById("add").onclick=function(){',
    'medications=readMedications();',
    'if(medications.length<MAX_MEDICATIONS){medications.push(blankMedication());render(medications.length-1);}',
    '};',
    'document.getElementById("settings").onsubmit=function(event){',
    'event.preventDefault();',
    'var values={',
    'morning:window.__timeToMinutes(document.getElementById("morning").value),',
    'noon:window.__timeToMinutes(document.getElementById("noon").value),',
    'evening:window.__timeToMinutes(document.getElementById("evening").value),',
    'night:window.__timeToMinutes(document.getElementById("night").value)',
    '};',
    'if(values.morning<0||values.noon<0||values.evening<0||values.night<0||!(values.morning<values.noon&&values.noon<values.evening&&values.evening<values.night)){',
    'alert(tr("Bitte die Startzeiten in der Reihenfolge Früh, Mittag, Abend und Nacht einstellen.","Please set the start times in the order morning, noon, evening and night."));',
    'document.getElementById("daypart-body").className="body";',
    'document.getElementById("daypart-panel").className="";',
    'return;',
    '}',
    'medications=readMedications();',
    'for(var medicationIndex=0;medicationIndex<medications.length;medicationIndex++){',
    'if(medications[medicationIndex].enabled&&!medications[medicationIndex].iconSet){alert(tr("Ein aktives Medikament benötigt ein vollständiges Icon.","An active medication requires a complete icon."));return;}',
    '}',
    'var result={theme:document.getElementById("theme").value,language:document.getElementById("language").value,dayparts:values,medications:medications,alarm:{audioEnabled:document.getElementById("audio-enabled").checked,audioVolume:parseInt(document.getElementById("audio-volume").value,10),vibrationEnabled:document.getElementById("vibration-enabled").checked,reminderInterval:reminderIntervals[parseInt(document.getElementById("reminder-interval").value,10)]},display:{showPattern:document.getElementById("show-pattern").checked}};',
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(result));',
    '};',
    'window.__timeToMinutes=' + timeToMinutes.toString() + ';',
    'window.__minutesToTime=' + minutesToTime.toString() + ';',
    'render(-1);',
    '</script>',
    '</body>',
    '</html>'
  ].join('');
}

Pebble.addEventListener('ready', function() {
  console.log('Nasu companion ready');
});

Pebble.addEventListener('showConfiguration', function() {
  var page = configurationPage(
    currentTheme(),
    currentLanguage(),
    currentDayparts(),
    currentMedications(),
    currentAlarmSettings(),
    currentDisplaySettings()
  );

  Pebble.openURL(
    'data:text/html;charset=utf-8,' +
    encodeURIComponent(page)
  );
});

Pebble.addEventListener('appmessage', function(event) {
  if (!event || !event.payload) {
    return;
  }

  var rawAcknowledged =
      event.payload[SETTINGS_ACK_KEY];

  if (typeof rawAcknowledged === 'undefined') {
    rawAcknowledged = event.payload.SETTINGS_ACK;
  }

  var acknowledged = parseInt(
    rawAcknowledged,
    10
  );
  var pending = parseInt(
    localStorage.getItem(
      SETTINGS_TRANSACTION_STORAGE_KEY
    ),
    10
  );

  if (
    !isFinite(acknowledged) ||
    acknowledged <= 0 ||
    !isFinite(pending) ||
    acknowledged !== pending
  ) {
    return;
  }

  localStorage.removeItem(
    SETTINGS_TRANSACTION_STORAGE_KEY
  );

  console.log(
    'Watch confirmed settings transaction ' +
    acknowledged +
    ' including intake reset'
  );
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event || !event.response) {
    return;
  }

  try {
    var settings = JSON.parse(
      decodeURIComponent(event.response)
    );

    if (
      settings.theme !== 'light' &&
      settings.theme !== 'dark'
    ) {
      return;
    }

    var language =
        settings.language === 'en'
            ? 'en'
            : 'de';

    var dayparts = normalizeDayparts(
      settings.dayparts
    );

    var medications = normalizeMedications(
      settings.medications
    );

    var alarm = normalizeAlarmSettings(
      settings.alarm
    );

    var display = normalizeDisplaySettings(
      settings.display
    );

    localStorage.setItem(
      THEME_STORAGE_KEY,
      settings.theme
    );

    localStorage.setItem(
      LANGUAGE_STORAGE_KEY,
      language
    );

    localStorage.setItem(
      DAYPART_STORAGE_KEY,
      JSON.stringify(dayparts)
    );

    localStorage.setItem(
      MEDICATIONS_STORAGE_KEY,
      JSON.stringify(medications)
    );

    localStorage.setItem(
      ALARM_STORAGE_KEY,
      JSON.stringify(alarm)
    );

    localStorage.setItem(
      DISPLAY_STORAGE_KEY,
      JSON.stringify(display)
    );

    sendAllSettings(
      settings.theme,
      language,
      dayparts,
      medications,
      alarm,
      display
    );
  } catch (error) {
    console.log(
      'Could not save settings: ' +
      error.message
    );
  }
});
