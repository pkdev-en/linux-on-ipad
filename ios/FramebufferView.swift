import MetalKit

/// Shows the guest framebuffer. The guest draws into RAM that the host reads directly,
/// so presenting a frame is: copy the bytes into a texture, draw one triangle.
final class FramebufferView: MTKView, MTKViewDelegate {
    struct Frame {
        let pixels: UnsafeRawPointer
        let width: Int
        let height: Int
        let strideBytes: Int
    }

    private let commandQueue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let frameProvider: () -> Frame?
    private var texture: MTLTexture?

    // One full-screen triangle; the guest format x8r8g8b8 (little-endian) is BGRA in memory.
    private static let shaderSource = """
    #include <metal_stdlib>
    using namespace metal;
    struct VOut { float4 position [[position]]; float2 uv; };
    vertex VOut vs(uint vid [[vertex_id]]) {
        float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));
        VOut o;
        o.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
        o.uv = float2(p.x, 1.0 - p.y);
        return o;
    }
    fragment float4 fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        return float4(tex.sample(s, in.uv).rgb, 1.0);
    }
    """

    /// `frameProvider` is called on the main thread once per display refresh.
    init?(frameProvider: @escaping () -> Frame?) {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue(),
              let library = try? device.makeLibrary(source: FramebufferView.shaderSource, options: nil),
              let vertex = library.makeFunction(name: "vs"),
              let fragment = library.makeFunction(name: "fs")
        else { return nil }

        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertex
        descriptor.fragmentFunction = fragment
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor) else { return nil }

        self.commandQueue = queue
        self.pipeline = pipeline
        self.frameProvider = frameProvider
        super.init(frame: .zero, device: device)
        colorPixelFormat = .bgra8Unorm
        framebufferOnly = true
        preferredFramesPerSecond = 60
        delegate = self
    }

    required init(coder: NSCoder) {
        fatalError("FramebufferView is created in code")
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let frame = frameProvider(),
              let drawable = currentDrawable,
              let pass = currentRenderPassDescriptor,
              let buffer = commandQueue.makeCommandBuffer()
        else { return }

        if texture == nil || texture?.width != frame.width || texture?.height != frame.height {
            let td = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .bgra8Unorm, width: frame.width, height: frame.height, mipmapped: false)
            td.usage = .shaderRead
            texture = device?.makeTexture(descriptor: td)
        }
        guard let texture else { return }
        texture.replace(region: MTLRegionMake2D(0, 0, frame.width, frame.height),
                        mipmapLevel: 0, withBytes: frame.pixels, bytesPerRow: frame.strideBytes)

        guard let encoder = buffer.makeRenderCommandEncoder(descriptor: pass) else { return }
        encoder.setRenderPipelineState(pipeline)
        encoder.setFragmentTexture(texture, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        buffer.present(drawable)
        buffer.commit()
    }
}
