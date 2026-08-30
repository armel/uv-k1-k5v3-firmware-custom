/**
 * 内置卫星频率库（SatNOGS 数据，构建时生成）
 * 生成时间：2026-08-29T13:50:31.412Z
 * 来源：https://db.satnogs.org/api/transmitters/?format=json&status=active
 * 覆盖卫星：398 颗
 *
 * 更新方式：node tools/k5web/build_freqdb.mjs
 * UMD：浏览器挂 window.K5WEB.freqdb，Node require 导出。
 */
const FREQDB = {
 "14129": {
  "up": 145987000,
  "down": 145987000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Engineering Beacon"
 },
 "14781": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "1k2 AFSK TLM"
 },
 "20439": {
  "up": 437051000,
  "down": 437051000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Secondary Payload"
 },
 "20442": {
  "up": 145840000,
  "down": 437150000,
  "mode": "BPSK",
  "type": "Transceiver",
  "desc": "BPSK 1k2 Transceiver"
 },
 "22825": {
  "up": 145850000,
  "down": 436795000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U FM"
 },
 "22826": {
  "up": 435772000,
  "down": 435772000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "BPSK CW carrier"
 },
 "24278": {
  "up": 435910000,
  "down": 435910000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U Digitalker"
 },
 "25397": {
  "up": 435225000,
  "down": 435225000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U 9k6 FSK"
 },
 "25544": {
  "up": 145990000,
  "down": 437800000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U FM - Voice Repeater CTCSS 67.0 Hz"
 },
 "25635": {
  "up": 2114000000,
  "down": 2296000000,
  "mode": "BPSK",
  "type": "Transceiver",
  "desc": "Radio B"
 },
 "26063": {
  "up": 437100000,
  "down": 437100000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "9k6 FSK AX25"
 },
 "26609": {
  "up": 435450000,
  "down": 435450000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "70cm General Beacon"
 },
 "26931": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "Mode V/V APRS AFSK"
 },
 "27607": {
  "up": 145850000,
  "down": 436795000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U FM Voice CTCSS 67.0 Hz"
 },
 "27844": {
  "up": 437470000,
  "down": 437470000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U TLM"
 },
 "27845": {
  "up": 436675000,
  "down": 436675000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 9k6 (G3RUH) TLM"
 },
 "27848": {
  "up": 436847500,
  "down": 436847500,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U CW Beacon"
 },
 "27939": {
  "up": 435353000,
  "down": 435353000,
  "mode": "DOKA",
  "type": "Transmitter",
  "desc": "Mode U FM Doka"
 },
 "28650": {
  "up": 145860000,
  "down": 145860000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Dutch Beacon CW"
 },
 "28895": {
  "up": 437463100,
  "down": 437463100,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U CW BEACON"
 },
 "29709": {
  "up": 436075000,
  "down": 436075000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U1"
 },
 "31130": {
  "up": 435245000,
  "down": 435245000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "32785": {
  "up": 1267600000,
  "down": 437475000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode L/U Digipeater"
 },
 "32791": {
  "up": 437485000,
  "down": 437485000,
  "mode": "FMN",
  "type": "Transmitter",
  "desc": "Mode U Digi"
 },
 "32953": {
  "up": 435353000,
  "down": 435353000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "435.353"
 },
 "33498": {
  "up": 437275000,
  "down": 437275000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW (JR5YBO)"
 },
 "33499": {
  "up": 437445000,
  "down": 437445000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U MSG"
 },
 "35932": {
  "up": 437500000,
  "down": 437500000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "PE0SAT - 1k2 BPSK AFSK Tones"
 },
 "35933": {
  "up": 435950000,
  "down": 435950000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Digipeater (Mobitex, idle mode: 10 sec interval)"
 },
 "35935": {
  "up": 437325000,
  "down": 437325000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "GFSK Telemetry"
 },
 "36122": {
  "up": 145825000,
  "down": 435675000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U FM"
 },
 "36799": {
  "up": 145980000,
  "down": 145980000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "DOWN: CW in FM, UP: DTMF commands"
 },
 "37224": {
  "up": 437305000,
  "down": 437305000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "AFSK Beacon"
 },
 "37839": {
  "up": 437275000,
  "down": 437275000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "37841": {
  "up": 437425000,
  "down": 437425000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "38251": {
  "up": 1268520000,
  "down": 1268520000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "B3 GNSS"
 },
 "38340": {
  "up": 437375000,
  "down": 437375000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW TLM"
 },
 "38735": {
  "up": 435265000,
  "down": 435265000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "38756": {
  "up": 437485000,
  "down": 437485000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U CW TLM"
 },
 "38760": {
  "up": 437600000,
  "down": 437600000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "TLM AFSK"
 },
 "38761": {
  "up": 437349000,
  "down": 437349000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 AX25"
 },
 "38763": {
  "up": 437405000,
  "down": 437405000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK 1k2 AX25"
 },
 "39087": {
  "up": 437425000,
  "down": 437425000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode 4800bps FSK CW"
 },
 "39133": {
  "up": 435215000,
  "down": 435215000,
  "mode": "DOKA",
  "type": "Transmitter",
  "desc": "Mode U - DOKA"
 },
 "39152": {
  "up": 437225000,
  "down": 437225000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "437.225 CW"
 },
 "39153": {
  "up": 437445000,
  "down": 437445000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "AFSK 1200"
 },
 "39161": {
  "up": 437250000,
  "down": 437250000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Beacon"
 },
 "39417": {
  "up": 437356000,
  "down": 437356000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "FSK1k2 TLM"
 },
 "39427": {
  "up": 145818000,
  "down": 145818000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode 1200bps RC-BPSK CW"
 },
 "39428": {
  "up": 145870000,
  "down": 145870000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "39430": {
  "up": 437250000,
  "down": 437250000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "4k8 GMSK"
 },
 "39431": {
  "up": 437365000,
  "down": 437365000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Downlink U"
 },
 "39432": {
  "up": 435060000,
  "down": 145947000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "FM to DSB"
 },
 "39435": {
  "up": 437405000,
  "down": 437405000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U SSTV"
 },
 "39438": {
  "up": 145980000,
  "down": 145980000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode 1200bps BPSK CW"
 },
 "39440": {
  "up": 437445000,
  "down": 437445000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "UHF 1K2 AFSK Telemetry"
 },
 "39444": {
  "up": 145960000,
  "down": 145960000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Mode U/V Linear"
 },
 "39445": {
  "up": 437305000,
  "down": 437305000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "39446": {
  "up": 436395000,
  "down": 436395000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Transmitter"
 },
 "39464": {
  "up": 437230000,
  "down": 437230000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Mode U - FSK19k2 AX25"
 },
 "39471": {
  "up": 437270000,
  "down": 437270000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 AX25"
 },
 "39492": {
  "up": 435265000,
  "down": 435265000,
  "mode": "PSK",
  "type": "Transmitter",
  "desc": "PSK"
 },
 "39497": {
  "up": 435465000,
  "down": 435465000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - FM"
 },
 "39765": {
  "up": 435465000,
  "down": 435465000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U TLM"
 },
 "40012": {
  "up": 437421500,
  "down": 437421500,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "9k6 FSK TLM"
 },
 "40014": {
  "up": 437445000,
  "down": 437445000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "437.445 AFSK 1200bps"
 },
 "40021": {
  "up": 145980000,
  "down": 145980000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW TLM beacon callsign 4X4HSL"
 },
 "40024": {
  "up": 145865000,
  "down": 145865000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode V - BPSK1k2 - TLM"
 },
 "40025": {
  "up": 145950000,
  "down": 145950000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Linear Transponder"
 },
 "40032": {
  "up": 145880000,
  "down": 145880000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "40042": {
  "up": 437675000,
  "down": 437675000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "9k6 FSK TLM"
 },
 "40043": {
  "up": 435000000,
  "down": 435000000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "9k6 FSK TLM"
 },
 "40054": {
  "up": 437250000,
  "down": 437250000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "40055": {
  "up": 145890000,
  "down": 145890000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "40056": {
  "up": 145890000,
  "down": 145890000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "40057": {
  "up": 145980000,
  "down": 145980000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "40074": {
  "up": 145940000,
  "down": 145940000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Inverting linear transponder"
 },
 "40320": {
  "up": 435270000,
  "down": 435270000,
  "mode": "LSB",
  "type": "Transmitter",
  "desc": "U/V Transponder"
 },
 "40719": {
  "up": 145975000,
  "down": 145975000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "1k2 BPSK TLM"
 },
 "40908": {
  "up": 144390000,
  "down": 144390000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "APRS Digipeater"
 },
 "40931": {
  "up": 145880000,
  "down": 435880000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "V/U FM Voice (PL 88.5Hz)"
 },
 "40958": {
  "up": 437650000,
  "down": 437650000,
  "mode": "MSK",
  "type": "Transmitter",
  "desc": "TLM"
 },
 "40974": {
  "up": 437375500,
  "down": 437375500,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "41338": {
  "up": 437094000,
  "down": 437094000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "41339": {
  "up": 437423000,
  "down": 437423000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "41340": {
  "up": 437375000,
  "down": 437375000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "41603": {
  "up": 437425000,
  "down": 437425000,
  "mode": "FFSK",
  "type": "Transmitter",
  "desc": "Mode 1200bps FFSK"
 },
 "41783": {
  "up": 437455000,
  "down": 437455000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U FSK1200 AX25"
 },
 "41789": {
  "up": 437650000,
  "down": 437650000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode 9600bps FSK"
 },
 "41847": {
  "up": 435710000,
  "down": 435710000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Telemetry"
 },
 "42716": {
  "up": 435765000,
  "down": 435765000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "42794": {
  "up": 437509000,
  "down": 437509000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "FM Beacon"
 },
 "42829": {
  "up": 435950000,
  "down": 435950000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW Beacon"
 },
 "43012": {
  "up": 437529000,
  "down": 437529000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mean frequency of Rocket Body Telemetries"
 },
 "43017": {
  "up": 435250000,
  "down": 145960000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode U/V FM Voice (no CTCSS any longer)"
 },
 "43186": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK TUBiX10",
  "type": "Transmitter",
  "desc": "AFSK 1k2 TLM"
 },
 "43187": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK TUBiX10",
  "type": "Transmitter",
  "desc": "AFSK 1k2 TLM"
 },
 "43188": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK TUBiX10",
  "type": "Transmitter",
  "desc": "AFSK 1k2 TLM"
 },
 "43189": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK TUBiX10",
  "type": "Transmitter",
  "desc": "AFSK 1k2 TLM"
 },
 "43485": {
  "up": 437250000,
  "down": 437250000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Telem"
 },
 "43677": {
  "up": 435280000,
  "down": 437390000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Telemetry GMSK9k6 AX.25"
 },
 "43678": {
  "up": 437500000,
  "down": 145900000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "FM VOICE"
 },
 "43679": {
  "up": 437200000,
  "down": 437200000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "High Speed 115kbpsGMSK"
 },
 "43681": {
  "up": 437505000,
  "down": 437505000,
  "mode": "WSJT",
  "type": "Transmitter",
  "desc": "WSJT"
 },
 "43700": {
  "up": 2400360000,
  "down": 10489860000,
  "mode": "USB",
  "type": "Transceiver",
  "desc": "International Emergency Frequency"
 },
 "43758": {
  "up": 437250000,
  "down": 437250000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Secondary"
 },
 "43768": {
  "up": 436730000,
  "down": 436730000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "GFSK4k8"
 },
 "43770": {
  "up": 145920000,
  "down": 145920000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "AFSK9k6 digital data"
 },
 "43772": {
  "up": 437150000,
  "down": 437150000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "FSK"
 },
 "43773": {
  "up": 437475000,
  "down": 437475000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "FSK"
 },
 "43784": {
  "up": 437275000,
  "down": 437275000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "GMSK9k6 TLM"
 },
 "43786": {
  "up": 145860000,
  "down": 145860000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "BPSK1k2 AX.25 TLM"
 },
 "43790": {
  "up": 435800000,
  "down": 435800000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "AIS Downlink"
 },
 "43792": {
  "up": 1263500000,
  "down": 145895000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "FM"
 },
 "43798": {
  "up": 437150000,
  "down": 437150000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK1k2"
 },
 "43803": {
  "up": 145865000,
  "down": 145865000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "U/V CW Transponder"
 },
 "43804": {
  "up": 437775000,
  "down": 437775000,
  "mode": "MSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "GMSK9k6 TLM"
 },
 "43879": {
  "up": 437325000,
  "down": 435525000,
  "mode": "DSTAR",
  "type": "Transceiver",
  "desc": "MODE U/U DSTAR VOICE"
 },
 "43880": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 9600 AX.25"
 },
 "44530": {
  "up": 145820000,
  "down": 436760000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U FM 67.0 PL"
 },
 "44881": {
  "up": 145925000,
  "down": 145925000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Mode U/V Linear Transponder"
 },
 "44885": {
  "up": 436100000,
  "down": 436100000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U/U GFSK2k4 Repeater"
 },
 "44909": {
  "up": 435640000,
  "down": 435640000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Mode V/U - Transponder"
 },
 "46493": {
  "up": 437000000,
  "down": 437000000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK4k8 MOBITEX"
 },
 "46494": {
  "up": 436700000,
  "down": 436700000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6"
 },
 "46495": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK TUBiX10",
  "type": "Transmitter",
  "desc": "AFSK 1k2 TLM"
 },
 "46504": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "46505": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "46506": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "46507": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "47945": {
  "up": 437425000,
  "down": 437425000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "TLM"
 },
 "48274": {
  "up": 145875000,
  "down": 436510000,
  "mode": "FMN",
  "type": "Transceiver",
  "desc": "2A V/U FM repeater NFM"
 },
 "49396": {
  "up": 437450000,
  "down": 437450000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK1k2"
 },
 "49399": {
  "up": 145875000,
  "down": 145875000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transmitter",
  "desc": "Mode V - 9K6 GMSK"
 },
 "49402": {
  "up": 435525000,
  "down": 435525000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode HF/U - Onboard SDR"
 },
 "50466": {
  "up": 435180000,
  "down": 435180000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U Audio"
 },
 "50993": {
  "up": 437515000,
  "down": 437515000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - FSK 1k2 AX.100"
 },
 "52897": {
  "up": 437485000,
  "down": 437485000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 9600 AX.25"
 },
 "52898": {
  "up": 436028500,
  "down": 436028500,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - BPSK1k2"
 },
 "52899": {
  "up": 436490000,
  "down": 436490000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U -  9k6 GMSK AX25 G3RUH TLM"
 },
 "52900": {
  "up": 436500000,
  "down": 436500000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - TLM"
 },
 "53105": {
  "up": 40000000000,
  "down": 40000000000,
  "mode": "CW",
  "type": "Transceiver",
  "desc": "532nm Retroreflector"
 },
 "53106": {
  "up": 435310000,
  "down": 435310000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transceiver",
  "desc": "Mode U PKT 1k2 GMSK"
 },
 "53107": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 AX25 G3RUH"
 },
 "53108": {
  "up": 435612500,
  "down": 435612500,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U -  GFSK 1kbps TLM"
 },
 "53109": {
  "up": 436750000,
  "down": 436750000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U TLM 2k4 GMSK AX25"
 },
 "53807": {
  "up": 437500000,
  "down": 437500000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U 2k4 TLM"
 },
 "54153": {
  "up": 435466000,
  "down": 435466000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U"
 },
 "55009": {
  "up": 437025000,
  "down": 437025000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 4k8 AX.25"
 },
 "55010": {
  "up": 2056600000,
  "down": 2245300000,
  "mode": "QPSK",
  "type": "Transceiver",
  "desc": "S-band TT&C transceiver (operator-provided)"
 },
 "55104": {
  "up": 437325000,
  "down": 437325000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6"
 },
 "55904": {
  "up": 437410000,
  "down": 437410000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Lander"
 },
 "56744": {
  "up": 437800000,
  "down": 437800000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - MSK4k8"
 },
 "56745": {
  "up": 436000000,
  "down": 436000000,
  "mode": "MSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - MSK4k8"
 },
 "56749": {
  "up": 435450000,
  "down": 435450000,
  "mode": "MSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - MSK4k8"
 },
 "56964": {
  "up": 436630000,
  "down": 436630000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "57168": {
  "up": 436000000,
  "down": 436000000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa: BW 125kHz, SF 7"
 },
 "57172": {
  "up": 437625000,
  "down": 437625000,
  "mode": "SSTV",
  "type": "Transmitter",
  "desc": "Mode U - SSTV"
 },
 "57173": {
  "up": 437402000,
  "down": 437402000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "SamSat-ION Beacon"
 },
 "57174": {
  "up": 435790000,
  "down": 435790000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "57175": {
  "up": 436990000,
  "down": 436990000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U 9k6 FSK AX.25"
 },
 "57178": {
  "up": 436570000,
  "down": 436570000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 4k8 (USP FEC) TLM"
 },
 "57179": {
  "up": 436700000,
  "down": 436700000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6"
 },
 "57180": {
  "up": 435290000,
  "down": 435290000,
  "mode": "SSTV",
  "type": "Transmitter",
  "desc": "Mode U - Robot 36 SSTV"
 },
 "57182": {
  "up": 436080000,
  "down": 436080000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 9k6 (USP FEC)"
 },
 "57183": {
  "up": 435050000,
  "down": 435050000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 62.5k"
 },
 "57184": {
  "up": 435860000,
  "down": 435860000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6"
 },
 "57186": {
  "up": 436270000,
  "down": 436270000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "57187": {
  "up": 437875000,
  "down": 437875000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 2k4 (USP FEC) TLM"
 },
 "57188": {
  "up": 435300000,
  "down": 435300000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 9K6"
 },
 "57189": {
  "up": 437825000,
  "down": 437825000,
  "mode": "SSTV",
  "type": "Transmitter",
  "desc": "SSTV - Robot 72"
 },
 "57191": {
  "up": 436550000,
  "down": 436550000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 62.5k"
 },
 "57195": {
  "up": 435615000,
  "down": 435615000,
  "mode": "DOKA",
  "type": "Transmitter",
  "desc": "Mode U - DOKA 9k6"
 },
 "57196": {
  "up": 435500000,
  "down": 435500000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "57200": {
  "up": 435400000,
  "down": 435400000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6"
 },
 "57202": {
  "up": 436075000,
  "down": 436075000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "57203": {
  "up": 436125000,
  "down": 436125000,
  "mode": "SSTV",
  "type": "Transmitter",
  "desc": "Mode U - Robot 36 SSTV"
 },
 "57206": {
  "up": 437050000,
  "down": 437050000,
  "mode": "SSTV",
  "type": "Transmitter",
  "desc": "Mode U - SSTV"
 },
 "57217": {
  "up": 437100000,
  "down": 437100000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK 4k8 (USP FEC) TLM"
 },
 "57482": {
  "up": 437125000,
  "down": 437125000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - FSK4k8"
 },
 "57484": {
  "up": 437500000,
  "down": 437500000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFKS9k6"
 },
 "57486": {
  "up": 436400000,
  "down": 436400000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "58262": {
  "up": 2049700000,
  "down": 2217900000,
  "mode": "QPSK",
  "type": "Transceiver",
  "desc": "S-band TT&C transceiver (operator-provided)"
 },
 "58755": {
  "up": 435950000,
  "down": 435950000,
  "mode": "FMN",
  "type": "Transmitter",
  "desc": "CW/FM (F2A) Beacon"
 },
 "58810": {
  "up": 435950000,
  "down": 435950000,
  "mode": "FMN",
  "type": "Transmitter",
  "desc": "CW/FM (F2A) Beacon"
 },
 "58818": {
  "up": 435000000,
  "down": 435000000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U FSK"
 },
 "58921": {
  "up": 437250000,
  "down": 437250000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - 9k6 GMSK"
 },
 "59065": {
  "up": 145000000,
  "down": 145000000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode V - ODS V/TTCR V"
 },
 "59112": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "Mode V/V - APRS digipeater"
 },
 "60209": {
  "up": 435800000,
  "down": 435800000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "UHF TLM 1k2 BPSK"
 },
 "60237": {
  "up": 436785000,
  "down": 436785000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U/U - GFSK9k6 - Digipeater - AX.25"
 },
 "60238": {
  "up": 145895000,
  "down": 145895000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode V - AFSK1k2"
 },
 "60240": {
  "up": 435950000,
  "down": 435950000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK1k2"
 },
 "60242": {
  "up": 436240000,
  "down": 436240000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "UHF 50k GMSK TLM"
 },
 "60243": {
  "up": 436750000,
  "down": 436750000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - AX.25"
 },
 "60246": {
  "up": 437185000,
  "down": 437185000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - FSK2k4"
 },
 "60476": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - Transmitter"
 },
 "60525": {
  "up": 436500000,
  "down": 436500000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "9k6 TLM"
 },
 "60530": {
  "up": 435410000,
  "down": 435410000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "UHF 9k6 Telemetry"
 },
 "60535": {
  "up": 435350000,
  "down": 435350000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "UHF 9k6 Telemetry"
 },
 "61048": {
  "up": 435900000,
  "down": 435900000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "61072": {
  "up": 436740000,
  "down": 436740000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - 38k4 2FSK"
 },
 "61746": {
  "up": 436835000,
  "down": 436835000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW Beacon"
 },
 "61749": {
  "up": 436870000,
  "down": 436870000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Telemetry (Geoscan framing)"
 },
 "61750": {
  "up": 435600000,
  "down": 435600000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "61753": {
  "up": 435650000,
  "down": 435650000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK38k4"
 },
 "61754": {
  "up": 435700000,
  "down": 435700000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK38k4"
 },
 "61769": {
  "up": 436740000,
  "down": 436740000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "61772": {
  "up": 436540000,
  "down": 436540000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK2k4 USP"
 },
 "61779": {
  "up": 435615000,
  "down": 435615000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - BPSK (DOKA) - 4k8"
 },
 "61781": {
  "up": 145850000,
  "down": 435400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transceiver"
 },
 "61784": {
  "up": 437400000,
  "down": 437400000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "FSK1k2 - AX.25 Beacon"
 },
 "61785": {
  "up": 436200000,
  "down": 436200000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW Beacon"
 },
 "62391": {
  "up": 436925000,
  "down": 436925000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U/U - GFSK9k6 - Digipeater - AX.25"
 },
 "62394": {
  "up": 436775000,
  "down": 436775000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U/U - GFSK9k6 - Digipeater - AX.25"
 },
 "62459": {
  "up": 436500000,
  "down": 436500000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U TLM"
 },
 "62460": {
  "up": 436500000,
  "down": 436500000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter - Intersatellite"
 },
 "62616": {
  "up": 437020000,
  "down": 437020000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "63210": {
  "up": 435185000,
  "down": 435185000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - BPSK9k6 - AX.25 G3RUH - Telemetry"
 },
 "63211": {
  "up": 436200000,
  "down": 436200000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "63213": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63214": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63215": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63217": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63218": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63219": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder - Beacon"
 },
 "63237": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63238": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder"
 },
 "63239": {
  "up": 145970000,
  "down": 436400000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transponder - Beacon"
 },
 "63240": {
  "up": 436500000,
  "down": 436500000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter AX.25 G3RUH"
 },
 "63298": {
  "up": 437125000,
  "down": 437125000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "64535": {
  "up": 145900000,
  "down": 145900000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode V - GFSK19k2 - Backup"
 },
 "64549": {
  "up": 145900000,
  "down": 145900000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode V - GFSK19k2 - Backup"
 },
 "64878": {
  "up": 435830000,
  "down": 435830000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64879": {
  "up": 436940000,
  "down": 436940000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64880": {
  "up": 435970000,
  "down": 435970000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64881": {
  "up": 436270000,
  "down": 436270000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Telemetry (Geoscan framing)"
 },
 "64882": {
  "up": 435575000,
  "down": 435575000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa telemetry"
 },
 "64883": {
  "up": 437162000,
  "down": 437162000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa telemetry"
 },
 "64890": {
  "up": 436160000,
  "down": 436160000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64891": {
  "up": 436660000,
  "down": 436660000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64892": {
  "up": 435335000,
  "down": 435335000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64893": {
  "up": 435740000,
  "down": 435740000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64894": {
  "up": 436436000,
  "down": 436436000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - AX.25 Beacon and Ham Payload (Geoscan framing)"
 },
 "64895": {
  "up": 436725000,
  "down": 436725000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa telemetry"
 },
 "66653": {
  "up": 437665000,
  "down": 437665000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - Transmitter"
 },
 "66670": {
  "up": 437390000,
  "down": 437390000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "12k5 GMSK TLM"
 },
 "66673": {
  "up": 2108000000,
  "down": 2210000000,
  "mode": "FSK",
  "type": "Transceiver",
  "desc": "Mode S - FSK400k - Transmitter"
 },
 "66681": {
  "up": 2108000000,
  "down": 2208000000,
  "mode": "FSK",
  "type": "Transceiver",
  "desc": "Mode S - FSK400k - Transmitter"
 },
 "66778": {
  "up": 437125000,
  "down": 437125000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode U/U - GMSK9k6 - Digipeater - Skylink"
 },
 "66993": {
  "up": 437719000,
  "down": 437719000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "67232": {
  "up": 435000000,
  "down": 435000000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "67253": {
  "up": 435792000,
  "down": 435792000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "67254": {
  "up": 435852000,
  "down": 435852000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "67255": {
  "up": 436740000,
  "down": 436740000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Telemetry"
 },
 "67264": {
  "up": 436100000,
  "down": 436100000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa (SF 8, BW 125)"
 },
 "67271": {
  "up": 437200000,
  "down": 437200000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U"
 },
 "67277": {
  "up": 435840000,
  "down": 435840000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "LoRa telemetry"
 },
 "67279": {
  "up": 435750000,
  "down": 435750000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK2k4"
 },
 "67290": {
  "up": 437350000,
  "down": 437350000,
  "mode": "GMSK USP",
  "type": "Transmitter",
  "desc": "Mode U - GMSK2k4 - USP"
 },
 "67291": {
  "up": 145920000,
  "down": 436950000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "V/U FM Transponder CTCSS 67.0 Hz"
 },
 "67683": {
  "up": 145825000,
  "down": 145825000,
  "mode": "FSK",
  "type": "Transceiver",
  "desc": "Mode V/V - FSK9k6 - Digipeater - AX.25 G3RUH"
 },
 "68261": {
  "up": 435860000,
  "down": 435860000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW"
 },
 "68417": {
  "up": 436300000,
  "down": 436300000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - Transmitter"
 },
 "68446": {
  "up": 145875000,
  "down": 436875000,
  "mode": "FSK",
  "type": "Transceiver",
  "desc": "Mode V/U - 200 bps UP, 800 bps DOWN - TLM, SSDV, Codec 2, BBS"
 },
 "68456": {
  "up": 437195000,
  "down": 437195000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "UHF TT&C"
 },
 "68458": {
  "up": 436965000,
  "down": 436965000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "AX.25 FSK9k6 BEACON"
 },
 "68506": {
  "up": 437325000,
  "down": 437325000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK19k2 - AX.25 G3RUH"
 },
 "68635": {
  "up": 437250000,
  "down": 437250000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "AstroDev Li-2"
 },
 "68795": {
  "up": 437505000,
  "down": 437505000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "69015": {
  "up": 436150000,
  "down": 436150000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6 - Downlink"
 },
 "69589": {
  "up": 435900000,
  "down": 435900000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Unconfirmed Transmitter"
 },
 "69591": {
  "up": 435900000,
  "down": 435900000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 2400"
 },
 "98247": {
  "up": 437443000,
  "down": 437443000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - BPSK9k6 TLM"
 },
 "98248": {
  "up": 145950000,
  "down": 435500000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "Mode V/U - FM Transceiver"
 },
 "98249": {
  "up": 435900000,
  "down": 435900000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK2k4 - Light-1 framing"
 },
 "98250": {
  "up": 435900000,
  "down": 435900000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK2k4 - Light-1 framing"
 },
 "98251": {
  "up": 435900000,
  "down": 435900000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK2k4 - Light-1 framing"
 },
 "98254": {
  "up": 437450000,
  "down": 437450000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - Telemetry"
 },
 "98256": {
  "up": 436980000,
  "down": 436980000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 14.7kHz/23.4kHz"
 },
 "98257": {
  "up": 435595000,
  "down": 435595000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98259": {
  "up": 435547000,
  "down": 435547000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98262": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98263": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98264": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98265": {
  "up": 437250000,
  "down": 437250000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U  - Transmitter"
 },
 "98266": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "Mode V/V - AFSK1k2 - Digipeater - AX.25"
 },
 "98271": {
  "up": 437575000,
  "down": 437575000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 AX100"
 },
 "98272": {
  "up": 145875000,
  "down": 145875000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transceiver",
  "desc": "Mode V/V Digipeater"
 },
 "98273": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "Mode V/V - AFSK1k2 - Digipeater - AX.25"
 },
 "98278": {
  "up": 435600000,
  "down": 435600000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 1"
 },
 "98280": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98281": {
  "up": 435600000,
  "down": 435600000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 1"
 },
 "98282": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98283": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98284": {
  "up": 437800000,
  "down": 437800000,
  "mode": "UNKNOWN",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98288": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 125KHz"
 },
 "98289": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 125KHz"
 },
 "98290": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 125KHz"
 },
 "98291": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 125KHz"
 },
 "98292": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 62.5KHz"
 },
 "98293": {
  "up": 145925000,
  "down": 145925000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode V/V - GFSK9k6 AX.25 G3RUH"
 },
 "98314": {
  "up": 437900000,
  "down": 437900000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98320": {
  "up": 436500000,
  "down": 436500000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U - BPSK9k6 TLM - 436.5MHz"
 },
 "98323": {
  "up": 436325000,
  "down": 436325000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "98324": {
  "up": 145850000,
  "down": 145850000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "98326": {
  "up": 437175000,
  "down": 437175000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6"
 },
 "98327": {
  "up": 437200000,
  "down": 437200000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "98329": {
  "up": 436830000,
  "down": 436830000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW"
 },
 "98333": {
  "up": 145900000,
  "down": 437125000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode V/U - GMSK9k6 - Digipeater"
 },
 "98334": {
  "up": 437235000,
  "down": 437235000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK 7416 - Payload"
 },
 "98335": {
  "up": 437350000,
  "down": 437350000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - AX.25 Beacon"
 },
 "98345": {
  "up": 437000000,
  "down": 437000000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98372": {
  "up": 437080000,
  "down": 437080000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK 1k2 AX.25 G3RUH"
 },
 "98373": {
  "up": 435615000,
  "down": 435615000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK1k2 - DOKA"
 },
 "98376": {
  "up": 436600000,
  "down": 436600000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK500 TLM"
 },
 "98381": {
  "up": 437075000,
  "down": 437075000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK4k8 - Transmitter"
 },
 "98391": {
  "up": 437850000,
  "down": 437850000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - 1k2 AFSK Telemetry"
 },
 "98395": {
  "up": 436870000,
  "down": 436870000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "KOSTKA UHF Digipeater"
 },
 "98398": {
  "up": 437375000,
  "down": 437375000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "CW"
 },
 "98416": {
  "up": 435225000,
  "down": 435225000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98420": {
  "up": 436995000,
  "down": 436995000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98421": {
  "up": 436350000,
  "down": 436350000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98423": {
  "up": 436550000,
  "down": 436550000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98424": {
  "up": 437600000,
  "down": 437600000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98425": {
  "up": 436700000,
  "down": 436700000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa - BW: 250KHz"
 },
 "98427": {
  "up": 435240000,
  "down": 435240000,
  "mode": "DOKA",
  "type": "Transmitter",
  "desc": "Mode U - DOKA"
 },
 "98429": {
  "up": 436320000,
  "down": 436320000,
  "mode": "SSDV",
  "type": "Transmitter",
  "desc": "Mode U - SSDV"
 },
 "98449": {
  "up": 437180000,
  "down": 437180000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "2k4 GMSK USP"
 },
 "98452": {
  "up": 437375000,
  "down": 437375000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK4k8"
 },
 "98465": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa (SF 6, BW 125, 15.625 bps)"
 },
 "98468": {
  "up": 437020000,
  "down": 437020000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - Transmitter"
 },
 "98470": {
  "up": 436300000,
  "down": 436300000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "UHF 9k6 Telemetry"
 },
 "98487": {
  "up": 436300000,
  "down": 436300000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - Transmitter"
 },
 "98488": {
  "up": 436300000,
  "down": 436300000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - Transmitter"
 },
 "98489": {
  "up": 437180000,
  "down": 437180000,
  "mode": "MSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98491": {
  "up": 437125000,
  "down": 437125000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98492": {
  "up": 436650000,
  "down": 436650000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - Transmitter"
 },
 "98493": {
  "up": 436500000,
  "down": 436500000,
  "mode": "FSK AX.100 Mode 5",
  "type": "Transceiver",
  "desc": "Mode U - FSK4k8 - AX.100"
 },
 "98494": {
  "up": 2030400000,
  "down": 2249510000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode S - GMSK38k4 - GOMSPACE NanoCom AX2150"
 },
 "98496": {
  "up": 436460000,
  "down": 436460000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6  - AX25 G3RUH - TLM"
 },
 "98497": {
  "up": 436788000,
  "down": 436788000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6  - AX25 G3RUH - TLM"
 },
 "98512": {
  "up": 436850000,
  "down": 436850000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - Transmitter"
 },
 "98517": {
  "up": 437010000,
  "down": 437010000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK9k6"
 },
 "98519": {
  "up": 436655000,
  "down": 436655000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK500 TLM"
 },
 "98526": {
  "up": 436025000,
  "down": 436025000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U - GFSK9k6 - Digipeater"
 },
 "98533": {
  "up": 145895000,
  "down": 435700000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode V/U - GMSK4k8 - Digital store & forward Transponder"
 },
 "98563": {
  "up": 436300000,
  "down": 436300000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98574": {
  "up": 435500000,
  "down": 435500000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98600": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "Mode V/V - AFSK1k2 - APRS Digipeater"
 },
 "98602": {
  "up": 437850000,
  "down": 437850000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK1k2"
 },
 "98630": {
  "up": 436500000,
  "down": 436500000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter"
 },
 "98649": {
  "up": 437080000,
  "down": 437080000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 1k2 AX.25 Beacon"
 },
 "98704": {
  "up": 436200000,
  "down": 436200000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "UHF 9k6 Telemetry"
 },
 "98705": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98706": {
  "up": 437050000,
  "down": 437050000,
  "mode": "CW",
  "type": "Transmitter",
  "desc": "Mode U - CW Beacon"
 },
 "98707": {
  "up": 145825000,
  "down": 145825000,
  "mode": "AFSK",
  "type": "Transceiver",
  "desc": "V/V 1k2 AFSK Digipeater"
 },
 "98724": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98767": {
  "up": 437985000,
  "down": 437985000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98768": {
  "up": 437050000,
  "down": 437050000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "[Experiment] LoRa (62.5 kHz bandwidth) (as FSK)"
 },
 "98769": {
  "up": 437485000,
  "down": 437485000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98770": {
  "up": 436570000,
  "down": 436570000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98771": {
  "up": 437350000,
  "down": 437350000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "Mode U - Transmitter 2"
 },
 "98775": {
  "up": 436350000,
  "down": 436350000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "[Experiment] LoRa (62.5 kHz bandwidth) (as FSK)"
 },
 "98776": {
  "up": 435250000,
  "down": 435250000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa 62.5k"
 },
 "98783": {
  "up": 437050000,
  "down": 437050000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - AFSK 1k135 Beacon TLM"
 },
 "98784": {
  "up": 436805000,
  "down": 436805000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "98796": {
  "up": 145895000,
  "down": 145895000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "Mode U TLM 1k2"
 },
 "98865": {
  "up": 437505000,
  "down": 437505000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK19k2"
 },
 "98878": {
  "up": 437350000,
  "down": 437350000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK14k4"
 },
 "98886": {
  "up": 437000000,
  "down": 437000000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK"
 },
 "98888": {
  "up": 437085000,
  "down": 437085000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6"
 },
 "98913": {
  "up": 437125000,
  "down": 437125000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - 9k6 FSK"
 },
 "98914": {
  "up": 435141000,
  "down": 435141000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 - AX.25 G3RUH"
 },
 "98972": {
  "up": 437450000,
  "down": 437450000,
  "mode": "GMSK",
  "type": "Transceiver",
  "desc": "Mode U/U - Transceiver"
 },
 "99047": {
  "up": 435730000,
  "down": 435730000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FM / AX.25"
 },
 "99054": {
  "up": 145975000,
  "down": 145975000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Mode U/V Transponder"
 },
 "99058": {
  "up": 437760000,
  "down": 437760000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFKS9k6"
 },
 "99060": {
  "up": 437120000,
  "down": 437120000,
  "mode": "GFSK",
  "type": "Transceiver",
  "desc": "Mode U - GFSK9k6 - EnduroSat UHF Transceiver Type II"
 },
 "99095": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transceiver",
  "desc": "Mode U Beacon"
 },
 "99152": {
  "up": 436550000,
  "down": 436550000,
  "mode": "AFSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK 1K2"
 },
 "99155": {
  "up": 436300000,
  "down": 436300000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "Mode U - GMSK9k6 - Telemetry"
 },
 "99156": {
  "up": 437475000,
  "down": 437475000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6 AX.25 G3RUH"
 },
 "99160": {
  "up": 436950000,
  "down": 436950000,
  "mode": "MSK AX.100 Mode 5",
  "type": "Transmitter",
  "desc": "Mode U - MSK4k8"
 },
 "99166": {
  "up": 435450000,
  "down": 435450000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U 9k6 AX.25 G3RUH"
 },
 "99206": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Primary Downlink / Uplink"
 },
 "99225": {
  "up": 436980000,
  "down": 436980000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "LoRa"
 },
 "99237": {
  "up": 437400000,
  "down": 437400000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "99380": {
  "up": 436500000,
  "down": 436500000,
  "mode": "FSK",
  "type": "Transmitter",
  "desc": "Mode U - FSK9k6"
 },
 "99396": {
  "up": 401000000,
  "down": 401000000,
  "mode": "FSK AX.25 G3RUH",
  "type": "Transceiver",
  "desc": "Mode U/U - GFSK 9k6 AX.25 G3RUH"
 },
 "99416": {
  "up": 437395000,
  "down": 437395000,
  "mode": "GFSK",
  "type": "Transmitter",
  "desc": "Mode U - GFSK1k2 - Transmitter"
 },
 "99484": {
  "up": 437290000,
  "down": 437290000,
  "mode": "LoRa",
  "type": "Transmitter",
  "desc": "Mode U - LoRa"
 },
 "99515": {
  "up": 437365000,
  "down": 437365000,
  "mode": "BPSK",
  "type": "Transmitter",
  "desc": "UHF 1K2 BPSK Telemetry"
 },
 "99913": {
  "up": 145980000,
  "down": 436225000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "V/U FM Repeater"
 },
 "99914": {
  "up": 435340000,
  "down": 145900000,
  "mode": "FM",
  "type": "Transceiver",
  "desc": "U/V FM CTCSS 67.0Hz"
 },
 "99926": {
  "up": 437425000,
  "down": 437425000,
  "mode": "FM",
  "type": "Transmitter",
  "desc": "TLM"
 },
 "99930": {
  "up": 437425000,
  "down": 437425000,
  "mode": "GMSK",
  "type": "Transmitter",
  "desc": "GMSK9k6 AX.25 TLM"
 },
 "07530": {
  "up": 145950000,
  "down": 145950000,
  "mode": "USB",
  "type": "Transmitter",
  "desc": "Mode U/V (B) Lin"
 }
};

(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.K5WEB = root.K5WEB || {};
    root.K5WEB.freqdb = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  return FREQDB;
});
