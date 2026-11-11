import Foundation
import AVFoundation
import AppKit
@main struct CheckCapture {
 static func main() async throws {
  let root=URL(fileURLWithPath:CommandLine.arguments[1])
  let asset=AVURLAsset(url:root)
  let video=try await asset.loadTracks(withMediaType:.video).first!
  let audio=try await asset.loadTracks(withMediaType:.audio).first!
  let size=try await video.load(.naturalSize), rate=try await video.load(.nominalFrameRate)
  let duration=try await asset.load(.duration)
  let formats=try await audio.load(.formatDescriptions)
  let asbd=CMAudioFormatDescriptionGetStreamBasicDescription(formats[0])!.pointee
  let reader=try AVAssetReader(asset:asset)
  let output=AVAssetReaderTrackOutput(track:audio,outputSettings:[AVFormatIDKey:kAudioFormatLinearPCM,AVLinearPCMIsFloatKey:true,AVLinearPCMBitDepthKey:32,AVLinearPCMIsNonInterleaved:false])
  reader.add(output);reader.startReading()
  var peak:Float=0, energy:Double=0;var samples=0, invalid=0
  while let buffer=output.copyNextSampleBuffer() {
   if let block=CMSampleBufferGetDataBuffer(buffer){
    var values=[Float](repeating:0,count:CMBlockBufferGetDataLength(block)/4)
    let status=values.withUnsafeMutableBytes{CMBlockBufferCopyDataBytes(block,atOffset:0,dataLength:$0.count,destination:$0.baseAddress!)}
    guard status==0 else {fatalError("PCM read failed")}
    for v in values {if !v.isFinite{invalid+=1};peak=max(peak,abs(v));energy+=Double(v*v);samples+=1}
   }
  }
  guard reader.status == .completed else {throw reader.error!}
  let generator=AVAssetImageGenerator(asset:asset);generator.appliesPreferredTrackTransform=true
  for second in [2,12]{
   let image=try generator.copyCGImage(at:CMTime(seconds:Double(second),preferredTimescale:600),actualTime:nil)
   let bitmap=NSBitmapImageRep(cgImage:image)
   try bitmap.representation(using:.png,properties:[:])!.write(to:root.deletingLastPathComponent().appendingPathComponent("OBS-Live-Frame-\(second).png"))
  }
  let report:[String:Any] = ["file":root.lastPathComponent,"duration_seconds":duration.seconds,"width":size.width,"height":size.height,"fps":rate,"sample_rate":asbd.mSampleRate,"channels":asbd.mChannelsPerFrame,"decoded_samples":samples,"audio_peak":peak,"audio_rms":sqrt(energy/Double(max(samples,1))),"nonfinite":invalid,"recorded_in_obs":true]
  let data=try JSONSerialization.data(withJSONObject:report,options:[.prettyPrinted,.sortedKeys]);try data.write(to:root.deletingLastPathComponent().appendingPathComponent("Recording-Verification.json"));print(String(data:data,encoding:.utf8)!)
 }
}
