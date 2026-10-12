// Native framebuffer stays exact. Only its CSS presentation scale changes.
export class Display {
    constructor(canvas, label, scale = 2) {
        this.canvas = canvas; this.context = canvas.getContext('2d', {alpha: false});
        this.target = ''; this.label = label; this.scale = scale; this.image = null;
        this.setScale(scale);
    }
    setScale(scale) {
        this.scale = Math.max(1, Math.min(9, Number(scale) || 1));
        this.canvas.style.width = this.canvas.width * this.scale + 'px';
        this.canvas.style.height = this.canvas.height * this.scale + 'px';
        this.label.textContent = `${this.target} · ${this.canvas.width} × ${this.canvas.height} native pixels · ${this.scale}× display`;
    }
    setProfile(profile) {
        this.target = profile.id;
        this.canvas.width = profile.width; this.canvas.height = profile.height;
        this.canvas.setAttribute('aria-label', profile.id + ' framebuffer');
        this.context.imageSmoothingEnabled = false;
        this.image = null;
        this.setScale(this.scale);
    }
    present(pixels, width, height, brightness) {
        if (!this.image || this.canvas.width !== width || this.canvas.height !== height) {
            this.canvas.width = width; this.canvas.height = height;
            this.context.imageSmoothingEnabled = false;
            this.image = this.context.createImageData(width, height);
            this.setScale(this.scale);
        }
        const out = this.image.data, factor = brightness / 255;
        for (let i = 0; i < pixels.length; i++) {
            const c = pixels[i], at = i * 4;
            out[at] = (c >> 11 & 31) * 255 / 31 * factor;
            out[at + 1] = (c >> 5 & 63) * 255 / 63 * factor;
            out[at + 2] = (c & 31) * 255 / 31 * factor;
            out[at + 3] = 255;
        }
        this.context.putImageData(this.image, 0, 0);
    }
    png() {return new Promise((resolve, reject) => this.canvas.toBlob(blob => blob ? resolve(blob) : reject(Error('Image unavailable')), 'image/png'));}
}
export function download(blob, name) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a'); a.href = url; a.download = name; a.click();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
}
