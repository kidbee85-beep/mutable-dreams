// A minimal macOS BLE-MIDI hardware probe for the uniquely named Mutable Dreams FM-1.
// Does not pair with or write to any unrelated device.
import Foundation
import CoreBluetooth

func log(_ message: String) { Swift.print(message); fflush(stdout) }

let midiService = CBUUID(string: "03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
let midiIO = CBUUID(string: "7772E5DB-3868-4112-A1A9-F2669D106BF3")
let clockMode = CommandLine.arguments.contains("--clock")

final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var manager: CBCentralManager!
    var peripheral: CBPeripheral?
    var connected = false
    var sent = false
    var pass = false
    var seen = 0

    override init() {
        super.init()
        manager = CBCentralManager(delegate: self, queue: DispatchQueue.main)
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log("bluetooth_state=\(central.state.rawValue) authorization=\(CBCentralManager.authorization.rawValue)")
        if central.state == .poweredOn {
            for p in central.retrieveConnectedPeripherals(withServices: [midiService]) {
                let name = p.name ?? ""
                log("already_connected_candidate=\(name)")
                guard name.hasPrefix("Mutable Dreams") else { continue }
                peripheral = p
                connected = true
                p.delegate = self
                p.discoverServices([midiService])
                return
            }
            central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
            log("scanning_for_mutable_dreams")
        }
    }
    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = p.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        seen += 1
        if name.localizedCaseInsensitiveContains("FM-1") || name.localizedCaseInsensitiveContains("Mutable") { log("candidate=\(name)") }
        guard name.hasPrefix("Mutable Dreams") else { return }
        log("found=\(name) rssi=\(RSSI)")
        central.stopScan()
        peripheral = p
        p.delegate = self
        central.connect(p)
    }
    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        connected = true
        log("connected=\(p.name ?? "?")")
        p.discoverServices([midiService])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        log("connect_failed=\(String(describing: error))")
    }
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil else { log("services_failed=\(error!)"); return }
        for service in p.services ?? [] where service.uuid == midiService { p.discoverCharacteristics([midiIO], for: service) }
    }
    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard error == nil else { log("characteristics_failed=\(error!)"); return }
        guard let c = s.characteristics?.first(where: { $0.uuid == midiIO }) else { return }
        log("midi_characteristic=\(c.properties.rawValue)")
        let kind: CBCharacteristicWriteType = c.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        // Test channel 9 only. USB telemetry confirms actual reception and release.
        let packets:[[UInt8]]=[[0xB8,64,127],[0xE8,0,0],[0xB8,11,0],[0x98,60,90],[0x88,60,0]]
        for (j,bytes) in packets.enumerated(){
            DispatchQueue.main.asyncAfter(deadline: .now()+0.12*Double(j)){
                p.writeValue(Data([0x80,0x80]+bytes),for:c,type:kind)
            }
        }
        DispatchQueue.main.asyncAfter(deadline: .now()+0.8){log("staged=1")}
        DispatchQueue.main.asyncAfter(deadline: .now()+7.0){self.manager.cancelPeripheralConnection(p)}

    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("disconnected=\(String(describing: error))")
        connected = false
        pass = true
    }
}

let probe = Probe()
let deadline = Date().addingTimeInterval(20)
while Date() < deadline && !probe.pass {
    RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(0.1))
}
if let p = probe.peripheral, probe.connected { probe.manager.cancelPeripheralConnection(p) }
log("seen=\(probe.seen) probe_result=\(probe.pass ? "disconnected_after_staging" : "unavailable")")
exit(probe.pass ? 0 : 1)
