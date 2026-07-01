import React from 'react';
import { Check, Loader2, Play, RefreshCw, Send } from 'lucide-react';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';

type FaceManifestItem = {
  value: string;
  label: string;
  asset: string;
  kind: 'bitmap';
  frameCount: number;
  fps: number;
};

type FaceManifest = {
  animations: FaceManifestItem[];
};

type FaceAnimation = {
  asset: string;
  trigger: string;
  label: string;
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  frameDelay: number;
  frames: number[][];
};

type LoadedFace = FaceManifestItem & {
  data?: FaceAnimation;
  loadError?: string;
};

interface FacePickerProps {
  currentAnimation?: string;
  isConnected: boolean;
  onSelect: (animation: string, task?: string) => Promise<boolean>;
  theme?: 'clean' | 'retro';
}

const drawBitmapFrame = (
  canvas: HTMLCanvasElement,
  animation: FaceAnimation,
  frameIndex: number,
) => {
  const context = canvas.getContext('2d');
  if (!context) return;

  const scale = 2;
  canvas.width = animation.width * scale;
  canvas.height = animation.height * scale;

  context.fillStyle = '#080a0d';
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.fillStyle = '#f7b65a';

  const frame = animation.frames[frameIndex % animation.frames.length];
  const bytesPerRow = animation.width / 8;

  for (let byteIndex = 0; byteIndex < frame.length; byteIndex++) {
    const sourceX = (byteIndex % bytesPerRow) * 8;
    const sourceY = Math.floor(byteIndex / bytesPerRow);

    for (let bit = 0; bit < 8; bit++) {
      if (frame[byteIndex] & (0x80 >> bit)) {
        context.fillRect((sourceX + bit) * scale, sourceY * scale, scale, scale);
      }
    }
  }
};

function FacePreview({ animation, active }: { animation?: FaceAnimation; active: boolean }) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);

  React.useEffect(() => {
    if (!animation || !canvasRef.current) return;

    let frame = 0;
    let stopped = false;
    let timeoutId: ReturnType<typeof setTimeout> | undefined;

    const render = () => {
      if (stopped || !canvasRef.current) return;
      drawBitmapFrame(canvasRef.current, animation, frame);
      frame = (frame + 1) % animation.frameCount;
      timeoutId = setTimeout(render, animation.frameDelay || Math.round(1000 / animation.fps));
    };

    render();

    return () => {
      stopped = true;
      if (timeoutId) clearTimeout(timeoutId);
    };
  }, [animation]);

  return (
    <div className={`relative aspect-[2/1] overflow-hidden rounded-md border bg-black ${active ? 'border-primary' : 'border-border'}`}>
      {animation ? (
        <canvas ref={canvasRef} className="h-full w-full [image-rendering:pixelated]" />
      ) : (
        <div className="flex h-full items-center justify-center text-xs text-muted-foreground">
          <Loader2 className="mr-2 h-4 w-4 animate-spin" />
          Loading
        </div>
      )}
    </div>
  );
}

const FacePicker: React.FC<FacePickerProps> = ({
  currentAnimation,
  isConnected,
  onSelect,
  theme = 'clean',
}) => {
  const [faces, setFaces] = React.useState<LoadedFace[]>([]);
  const [loading, setLoading] = React.useState(true);
  const [error, setError] = React.useState('');
  const [sending, setSending] = React.useState<string | null>(null);

  const loadFaces = React.useCallback(async () => {
    setLoading(true);
    setError('');

    try {
      const manifestResponse = await fetch('/animations/manifest.json', { cache: 'no-store' });
      if (!manifestResponse.ok) throw new Error('Failed to load animation manifest');
      const manifest = (await manifestResponse.json()) as FaceManifest;

      const loadedFaces = await Promise.all(
        manifest.animations.map(async (face) => {
          try {
            const response = await fetch(`/animations/${face.asset}.json`, { cache: 'no-store' });
            if (!response.ok) throw new Error(`Failed to load ${face.asset}`);
            const data = (await response.json()) as FaceAnimation;
            return { ...face, data };
          } catch (faceError) {
            return {
              ...face,
              loadError: faceError instanceof Error ? faceError.message : 'Failed to load face',
            };
          }
        }),
      );

      setFaces(loadedFaces);
    } catch (loadError) {
      setError(loadError instanceof Error ? loadError.message : 'Failed to load faces');
    } finally {
      setLoading(false);
    }
  }, []);

  React.useEffect(() => {
    loadFaces();
  }, [loadFaces]);

  const handleSelect = async (face: LoadedFace) => {
    setSending(face.value);
    try {
      await onSelect(face.value, `UI face picker: ${face.label}`);
    } finally {
      setSending(null);
    }
  };

  return (
    <Card className={`mt-6 ${theme === 'retro' ? 'border-2 border-black rounded-2xl shadow-[4px_4px_0_0_rgba(0,0,0,0.3)]' : ''}`}>
      <CardHeader className="pb-4">
        <div className="flex items-center justify-between gap-3">
          <CardTitle className="text-lg">Face Picker</CardTitle>
          <Button variant="outline" size="sm" onClick={loadFaces} disabled={loading}>
            <RefreshCw className={`mr-2 h-4 w-4 ${loading ? 'animate-spin' : ''}`} />
            Refresh
          </Button>
        </div>
      </CardHeader>
      <CardContent>
        {error && (
          <div className="mb-4 rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-sm text-destructive">
            {error}
          </div>
        )}

        <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
          {faces.map((face) => {
            const active = currentAnimation === face.value;
            const disabled = !isConnected || Boolean(face.loadError) || sending === face.value;

            return (
              <button
                key={face.value}
                type="button"
                onClick={() => handleSelect(face)}
                disabled={disabled}
                className={`rounded-lg border bg-card p-3 text-left transition hover:border-primary/70 hover:bg-accent/50 disabled:cursor-not-allowed disabled:opacity-60 ${active ? 'border-primary ring-2 ring-primary/25' : 'border-border'}`}
              >
                <FacePreview animation={face.data} active={active} />

                <div className="mt-3 flex items-start justify-between gap-2">
                  <div className="min-w-0">
                    <div className="truncate text-sm font-medium">{face.label}</div>
                    <div className="mt-0.5 text-xs text-muted-foreground">
                      {face.value}
                    </div>
                  </div>
                  {active ? (
                    <Badge variant="default" className="shrink-0">
                      <Check className="mr-1 h-3 w-3" />
                      Active
                    </Badge>
                  ) : (
                    <Badge variant="outline" className="shrink-0">
                      {face.frameCount}f
                    </Badge>
                  )}
                </div>

                <div className="mt-3 flex items-center justify-between gap-2 text-xs text-muted-foreground">
                  <span>{face.fps} fps</span>
                  <span className="inline-flex items-center gap-1">
                    {sending === face.value ? (
                      <Loader2 className="h-3.5 w-3.5 animate-spin" />
                    ) : isConnected ? (
                      <Send className="h-3.5 w-3.5" />
                    ) : (
                      <Play className="h-3.5 w-3.5" />
                    )}
                    {isConnected ? 'Set face' : 'Connect first'}
                  </span>
                </div>

                {face.loadError && (
                  <div className="mt-2 text-xs text-destructive">{face.loadError}</div>
                )}
              </button>
            );
          })}
        </div>

        {!loading && faces.length === 0 && !error && (
          <div className="rounded-md border border-dashed p-6 text-center text-sm text-muted-foreground">
            No faces found.
          </div>
        )}
      </CardContent>
    </Card>
  );
};

export default FacePicker;
