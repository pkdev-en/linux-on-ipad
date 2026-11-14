import Foundation

/// Owns one emulated machine and runs it on a background queue, in slices so it can be stopped.
final class VirtualMachine {
    struct Config {
        var kernel: String
        var initrd: String?
        var cmdline: String?
        var ramMB: UInt64 = 1024
        var width: UInt32 = 1280
        var height: UInt32 = 800
    }

    enum VMError: Error {
        case create(String)
    }

    private let handle: OpaquePointer
    private let queue = DispatchQueue(label: "lxemu.vm", qos: .userInteractive)
    private let lock = NSLock()
    private var wantsToRun = false

    init(config: Config) throws {
        var errorBuffer = [CChar](repeating: 0, count: 256)
        let errorLength = errorBuffer.count

        let created: OpaquePointer? = config.kernel.withCString { kernel in
            Self.withOptionalCString(config.initrd) { initrd in
                Self.withOptionalCString(config.cmdline) { cmdline in
                    var cfg = LxVmConfig()
                    cfg.kernel = kernel
                    cfg.initrd = initrd
                    cfg.cmdline = cmdline
                    cfg.ram_mb = config.ramMB
                    cfg.fb_width = config.width
                    cfg.fb_height = config.height
                    return lx_vm_create(&cfg, &errorBuffer, errorLength)
                }
            }
        }
        guard let created else {
            throw VMError.create(String(cString: errorBuffer))
        }
        handle = created
    }

    deinit {
        setRunning(false)
        queue.sync {}  // wait for the run loop to leave the core before freeing it
        lx_vm_destroy(handle)
    }

    private static func withOptionalCString<R>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
        if let string { return string.withCString { body($0) } }
        return body(nil)
    }

    private func setRunning(_ value: Bool) {
        lock.lock()
        wantsToRun = value
        lock.unlock()
    }

    private var isWanted: Bool {
        lock.lock()
        defer { lock.unlock() }
        return wantsToRun
    }

    func start() {
        guard !isWanted else { return }
        setRunning(true)
        queue.async { [self] in
            while isWanted {
                if !lx_vm_run(handle, 2_000_000) { break }  // machine stopped (halt, power off or fault)
            }
            setRunning(false)
        }
    }

    func stop() {
        setRunning(false)
    }

    /// The current guest frame, read straight from guest RAM (no copy).
    func currentFrame() -> FramebufferView.Frame? {
        var width: UInt32 = 0, height: UInt32 = 0, stride: UInt32 = 0
        guard let pixels = lx_vm_framebuffer(handle, &width, &height, &stride) else { return nil }
        return FramebufferView.Frame(pixels: UnsafeRawPointer(pixels), width: Int(width),
                                     height: Int(height), strideBytes: Int(stride))
    }

    func key(evdevCode: UInt16, down: Bool) { lx_vm_key(handle, evdevCode, down) }

    func pointer(x: Int32, y: Int32, leftDown: Bool) { lx_vm_pointer(handle, x, y, leftDown) }
}
