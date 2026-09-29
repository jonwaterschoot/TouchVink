// Web MIDI transport: telemetry SysEx in from the device, and the page's
// gestures out to it (see core/input.ts). Holding the port in the browser also
// sidesteps Windows' single-client MIDI: a DAW can't share it, but the page
// can play the device itself.

import type { Transport } from './transport';
import type { DeviceStore } from '../core/state';
import { applySysex, encodeRequest } from '../core/protocol';
import { midiOut } from './output';

const DEVICE_NAME = /touchvink|daisy|seed/i;

export class MidiTransport implements Transport {
  readonly kind = 'midi';
  private access: MIDIAccess | null = null;
  private input: MIDIInput | null = null;
  private output: MIDIOutput | null = null;

  constructor(private store: DeviceStore) {}

  async connect(): Promise<void> {
    this.access = await navigator.requestMIDIAccess({ sysex: true });
    this.access.onstatechange = () => this.pickPorts();
    this.pickPorts();
    if (!this.input) throw new Error('No MIDI input found — is TouchVink plugged in?');
    midiOut.sender = (bytes) => this.send(bytes);
  }

  disconnect(): void {
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    this.output = null;
    if (this.access) this.access.onstatechange = null;
    this.access = null;
    midiOut.sender = null;
    this.store.setConnected(false);
    this.store.reset();
  }

  describe(): string {
    if (!this.input) return 'MIDI: no device';
    return `MIDI: ${this.input.name ?? 'in'}${this.output ? ` / ${this.output.name}` : ''}`;
  }

  send(bytes: number[]): void {
    this.output?.send(bytes);
  }

  private pickPorts() {
    if (!this.access) return;
    const pick = <T extends MIDIPort>(ports: T[]) =>
      ports.find((p) => DEVICE_NAME.test(p.name ?? '')) ?? ports[0] ?? null;
    const input = pick([...this.access.inputs.values()]);
    if (input !== this.input) {
      if (this.input) this.input.onmidimessage = null;
      this.input = input;
      if (input) {
        input.onmidimessage = (e: MIDIMessageEvent) => {
          if (e.data) applySysex(e.data, this.store);
        };
      }
    }
    this.output = pick([...this.access.outputs.values()]);
    this.store.setConnected(!!this.input);
    if (this.output) this.send(encodeRequest());  // ask for a full snapshot now
  }
}
