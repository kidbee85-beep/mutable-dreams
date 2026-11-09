// Temporary BLE-MIDI peripheral for the FM-1 central-role hardware test.
// Advertises a unique test name and emits only a short note and CC74.
import Foundation
import CoreBluetooth

let serviceID = CBUUID(string: "03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
let ioID = CBUUID(string: "7772E5DB-3868-4112-A1A9-F2669D106BF3")

final class Sender: NSObject, CBPeripheralManagerDelegate {
    var manager: CBPeripheralManager!
    var characteristic: CBMutableCharacteristic?
    var subscribed = false
    override init() {
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: DispatchQueue.main)
    }
    func peripheralManagerDidUpdateState(_ manager: CBPeripheralManager) {
        print("bluetooth_state=\(manager.state.rawValue)"); fflush(stdout)
        guard manager.state == .poweredOn else { return }
        let c = CBMutableCharacteristic(type: ioID, properties: [.read, .notify, .writeWithoutResponse],
                                        value: nil, permissions: [.readable, .writeable])
        characteristic = c
        let s = CBMutableService(type: serviceID, primary: true)
        s.characteristics = [c]
        manager.add(s)
    }
    func peripheralManager(_ manager: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        if let e = error { print("service_error=\(e)"); fflush(stdout); return }
        manager.startAdvertising([CBAdvertisementDataLocalNameKey: "MD Test Sender",
                                  CBAdvertisementDataServiceUUIDsKey: [serviceID]])
    }
    func peripheralManagerDidStartAdvertising(_ manager: CBPeripheralManager, error: Error?) {
        print(error == nil ? "advertising=MD Test Sender" : "advertise_error=\(error!)")
        fflush(stdout)
    }
    func peripheralManager(_ manager: CBPeripheralManager, central: CBCentral, didSubscribeTo c: CBCharacteristic) {
        print("subscribed_central=\(central.identifier)"); fflush(stdout)
        subscribed = true
        guard let midi = characteristic else { return }
        let packets: [(Double, [UInt8], String)] = [
            (0.4, [0x80,0x80,0x90,60,90], "note_on"),
            (1.1, [0x80,0x81,0xB0,74,52], "cc74"),
            (2.1, [0x80,0x82,0x80,60,0], "note_off")
        ]
        for (delay, bytes, label) in packets {
            DispatchQueue.main.asyncAfter(deadline: .now()+delay) {
                let ok = manager.updateValue(Data(bytes), for: midi, onSubscribedCentrals: [central])
                print("sent=\(label) ok=\(ok)"); fflush(stdout)
            }
        }
    }
    func peripheralManager(_ manager: CBPeripheralManager, didReceiveRead req: CBATTRequest) {
        req.value = Data(); manager.respond(to: req, withResult: .success)
    }
    func peripheralManager(_ manager: CBPeripheralManager, didReceiveWrite reqs: [CBATTRequest]) {
        for req in reqs { manager.respond(to: req, withResult: .success) }
    }
}

let sender = Sender()
let end = Date().addingTimeInterval(35)
while Date() < end { RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(0.1)) }
print("sender_result=\(sender.subscribed ? "subscribed" : "no_connection")")
