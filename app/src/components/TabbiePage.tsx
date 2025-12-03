import React, { useState, useEffect, useRef } from 'react';
import { Wifi, WifiOff, CheckCircle, RefreshCw, Play, Square, RotateCcw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useTabbieSync } from '@/contexts/TabbieContext';

// Servo position constants (matching firmware)
const SERVO_LEFT = 15;
const SERVO_CENTER = 90;
const SERVO_RIGHT = 165;

// Servo patterns for each animation mode
const servoPatterns = {
  idle: {
    name: 'Idle',
    description: 'Every 4th loop',
    animationName: 'idle',
    keyframes: [
      { frame: 1, position: SERVO_CENTER, label: 'center' },
      { frame: 25, position: SERVO_LEFT, label: 'left' },
      { frame: 50, position: SERVO_CENTER, label: 'center' },
      { frame: 75, position: SERVO_RIGHT, label: 'right' },
      { frame: 90, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 97,
    fps: 12,
  },
  focus: {
    name: 'Focus',
    description: 'At 50% timer only',
    animationName: 'focus',
    keyframes: [
      { frame: 1, position: SERVO_CENTER, label: 'center' },
      { frame: 30, position: SERVO_LEFT + 15, label: 'slight left' },
      { frame: 50, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 65,
    fps: 8,
  },
  break: {
    name: 'Break',
    description: 'Every loop',
    animationName: 'break',
    keyframes: [
      { frame: 1, position: SERVO_CENTER, label: 'center' },
      { frame: 40, position: SERVO_LEFT, label: 'left' },
      { frame: 50, position: SERVO_RIGHT, label: 'right' },
      { frame: 60, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 65,
    fps: 8,
  },
  love: {
    name: 'Love/Complete',
    description: 'Celebratory wiggle',
    animationName: 'love',
    keyframes: [
      { frame: 5, position: SERVO_LEFT, label: 'left' },
      { frame: 15, position: SERVO_RIGHT, label: 'right' },
      { frame: 25, position: SERVO_LEFT, label: 'left' },
      { frame: 35, position: SERVO_RIGHT, label: 'right' },
      { frame: 45, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 65,
    fps: 8,
  },
  paused: {
    name: 'Paused (Angry)',
    description: 'Every 30 seconds',
    animationName: 'paused',
    keyframes: [
      { frame: 1, position: SERVO_CENTER, label: 'center' },
      { frame: 10, position: SERVO_LEFT, label: 'left' },
      { frame: 20, position: SERVO_RIGHT, label: 'right' },
      { frame: 30, position: SERVO_LEFT, label: 'left' },
      { frame: 40, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 45,
    fps: 10,
  },
  startup: {
    name: 'Startup',
    description: 'Once on boot',
    animationName: 'idle',
    keyframes: [
      { frame: 1, position: SERVO_CENTER, label: 'center' },
      { frame: 15, position: SERVO_LEFT, label: 'left' },
      { frame: 30, position: SERVO_RIGHT, label: 'right' },
      { frame: 45, position: SERVO_CENTER, label: 'center' },
    ],
    totalFrames: 53,
    fps: 8,
  },
};

interface TabbiePageProps {
  onPageChange?: (page: 'dashboard' | 'yourtabbie' | 'tasks' | 'reminders' | 'events' | 'notifications' | 'pomodoro' | 'calendar' | 'activity' | 'timetracking' | 'settings' | 'notes') => void;
  theme?: 'clean' | 'retro';
}

const TabbiePage: React.FC<TabbiePageProps> = ({ theme = 'clean' }) => {
  const {
    isConnected,
    isConnecting,
    tabbieStatus,
    connectionError,
    customIP,
    checkConnection,
    setCustomIP: setCustomIPContext,
    activityState,
    disconnect,
    sendAnimation
  } = useTabbieSync();

  const [localIP, setLocalIP] = React.useState(customIP);
  const [isResetting, setIsResetting] = React.useState(false);

  // ========================================
  // SERVO SIMULATOR STATE (Simplified)
  // ========================================
  const [selectedPattern, setSelectedPattern] = useState<keyof typeof servoPatterns>('idle');
  const [isPlaying, setIsPlaying] = useState(false);
  const [frame, setFrame] = useState(0);
  const [servoAngle, setServoAngle] = useState(SERVO_CENTER);
  const [activeKeyframeIndex, setActiveKeyframeIndex] = useState(-1);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const handleResetWiFi = async () => {
    if (!confirm('This will clear WiFi settings and restart Tabbie in setup mode. Continue?')) return;
    
    setIsResetting(true);
    try {
      await fetch(`http://${customIP}/api/reset`, { method: 'POST' });
      disconnect();
    } catch (e) {
      console.log('Reset sent, Tabbie is restarting...');
      disconnect();
    }
    setIsResetting(false);
  };

  React.useEffect(() => {
    setLocalIP(customIP);
  }, [customIP]);

  // ========================================
  // ANIMATION LOOP - Simple setInterval
  // ========================================
  useEffect(() => {
    if (!isPlaying) return;

    const pattern = servoPatterns[selectedPattern];
    const msPerFrame = 1000 / pattern.fps;

    // Clear any existing interval
    if (intervalRef.current) {
      clearInterval(intervalRef.current);
    }

    intervalRef.current = setInterval(() => {
      setFrame(prevFrame => {
        const nextFrame = prevFrame + 1;
        
        // Check if this frame is a keyframe
        const keyframeIdx = pattern.keyframes.findIndex(kf => kf.frame === nextFrame);
        if (keyframeIdx !== -1) {
          const kf = pattern.keyframes[keyframeIdx];
          console.log(`🎯 Frame ${nextFrame}: servo → ${kf.position}° (${kf.label})`);
          setServoAngle(kf.position);
          setActiveKeyframeIndex(keyframeIdx);
        }

        // Loop back to 0 at end
        if (nextFrame >= pattern.totalFrames) {
          console.log(`🔄 Loop complete, restarting...`);
          setActiveKeyframeIndex(-1);
          return 0;
        }

        return nextFrame;
      });
    }, msPerFrame);

    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    };
  }, [isPlaying, selectedPattern]);

  // ========================================
  // CONTROL FUNCTIONS
  // ========================================
  const startSimulation = () => {
    console.log(`▶️ Starting ${selectedPattern} simulation`);
    setFrame(0);
    setServoAngle(SERVO_CENTER);
    setActiveKeyframeIndex(-1);
    setIsPlaying(true);
  };

  const stopSimulation = () => {
    console.log(`⏹️ Stopping simulation`);
    setIsPlaying(false);
    if (intervalRef.current) {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
  };

  const resetSimulation = () => {
    stopSimulation();
    setFrame(0);
    setServoAngle(SERVO_CENTER);
    setActiveKeyframeIndex(-1);
  };

  const playAndSendToTabbie = () => {
    // Start local simulation
    startSimulation();

    // Send to Tabbie device
    if (isConnected) {
      const pattern = servoPatterns[selectedPattern];
      console.log(`📡 Sending ${pattern.animationName} to Tabbie`);
      sendAnimation(pattern.animationName, `Testing ${selectedPattern}`);
    }
  };

  // Send servo position directly to Tabbie
  const sendServoPosition = async (position: number | string) => {
    if (!isConnected) return;
    try {
      await fetch(`http://${customIP}/api/servo`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ position })
      });
      // Update local display
      const angle = typeof position === 'string' 
        ? (position === 'left' ? SERVO_LEFT : position === 'right' ? SERVO_RIGHT : SERVO_CENTER)
        : position;
      setServoAngle(angle);
    } catch (e) {
      console.error('Failed to send servo position:', e);
    }
  };

  const formatUptime = (uptime: number) => {
    const seconds = Math.floor(uptime / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
  };

  return (
    <div className="min-h-screen bg-background">
      {/* Header */}
      <div className="bg-card border-b">
        <div className="max-w-6xl mx-auto px-6 py-4">
          <div className="flex items-center justify-between">
            <div>
              <h1 className="text-2xl font-bold text-foreground flex items-center gap-2">
                🤖 Tabbie
                {isConnected ? (
                  <Wifi className="h-5 w-5 text-green-500" />
                ) : (
                  <WifiOff className="h-5 w-5 text-red-500" />
                )}
              </h1>
              <p className="text-muted-foreground text-sm">
                {isConnected ? 'Connected' : 'Not connected'}
              </p>
            </div>
            {/* Only show refresh button when connected */}
            {isConnected && (
              <Button
                onClick={checkConnection}
                disabled={isConnecting}
                variant="outline"
                size="sm"
              >
                <RefreshCw className={`h-4 w-4 mr-2 ${isConnecting ? 'animate-spin' : ''}`} />
                Refresh
              </Button>
            )}
          </div>
        </div>
      </div>

      <div className="max-w-2xl mx-auto px-6 py-8">
        {/* Not Connected */}
        {!isConnected && (
          <Card className={theme === 'retro' ? "border-2 border-black rounded-2xl shadow-[4px_4px_0_0_rgba(0,0,0,0.3)]" : ""}>
            <CardHeader className="pb-4">
              <CardTitle className="text-lg">Connect to Tabbie</CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              {/* Simple instruction */}
              <p className="text-sm text-muted-foreground">
                Press the <strong>BOOT button</strong> on your Tabbie to see the IP address, then enter it below.
              </p>

              {/* Connection input */}
              <div className="flex gap-2">
                <Input
                  placeholder="tabbie.local or IP address"
                  value={localIP}
                  onChange={(e) => setLocalIP(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') {
                      setCustomIPContext(localIP);
                      checkConnection();
                    }
                  }}
                  className="flex-1"
                />
                <Button 
                  onClick={() => {
                    setCustomIPContext(localIP);
                    checkConnection();
                  }} 
                  disabled={isConnecting}
                >
                  {isConnecting ? 'Connecting...' : 'Connect'}
                </Button>
              </div>

              {/* Error message */}
              {connectionError && (
                <p className="text-sm text-red-600 dark:text-red-400">
                  {connectionError}
                </p>
              )}

              {/* First time setup - collapsed by default */}
              <details className="text-sm">
                <summary className="cursor-pointer text-muted-foreground hover:text-foreground">
                  First time setup?
                </summary>
                <div className="mt-2 pl-4 border-l-2 border-muted space-y-1 text-muted-foreground">
                  <p>1. Connect to <strong>"Tabbie-Setup"</strong> WiFi</p>
                  <p>2. Open <strong>192.168.4.1</strong> in browser</p>
                  <p>3. Select your WiFi and enter password</p>
                  <p>4. Tabbie will connect and show its IP</p>
                </div>
              </details>
            </CardContent>
          </Card>
        )}

        {/* Connected */}
        {isConnected && tabbieStatus && (
          <Card className={theme === 'retro' ? "border-2 border-black rounded-2xl shadow-[4px_4px_0_0_rgba(0,0,0,0.3)]" : ""}>
            <CardHeader className="pb-4">
              <CardTitle className="text-lg flex items-center gap-2">
                <CheckCircle className="h-5 w-5 text-green-500" />
                Connected
              </CardTitle>
            </CardHeader>
            <CardContent>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <div className="text-xs text-muted-foreground">State</div>
                  <div className="font-medium">
                    {activityState === 'focus' && <span className="text-red-500">🍅 Focus</span>}
                    {activityState === 'break' && <span className="text-green-500">☕ Break</span>}
                    {activityState === 'complete' && <span className="text-blue-500">✅ Complete</span>}
                    {activityState === 'paused' && <span className="text-orange-500">⏸️ Paused</span>}
                    {activityState === 'idle' && <span className="text-gray-500">💤 Idle</span>}
                  </div>
                </div>
                <div>
                  <div className="text-xs text-muted-foreground">IP</div>
                  <div className="font-medium font-mono text-sm">{tabbieStatus.ip}</div>
                </div>
                <div>
                  <div className="text-xs text-muted-foreground">Uptime</div>
                  <div className="font-medium">{formatUptime(tabbieStatus.uptime)}</div>
                </div>
                <div>
                  <div className="text-xs text-muted-foreground">Animation</div>
                  <div className="font-medium capitalize">{tabbieStatus.animation}</div>
                </div>
              </div>

              {tabbieStatus.task && (
                <div className="mt-4 p-2 bg-muted rounded text-sm">
                  <span className="text-muted-foreground">Task:</span> {tabbieStatus.task}
                </div>
              )}

              {/* Reset WiFi */}
              <div className="mt-4 pt-4 border-t">
                <Button
                  variant="ghost"
                  size="sm"
                  onClick={handleResetWiFi}
                  disabled={isResetting}
                  className="text-muted-foreground hover:text-destructive"
                >
                  {isResetting ? 'Resetting...' : 'Reset WiFi'}
                </Button>
              </div>
            </CardContent>
          </Card>
        )}

        {/* Servo Simulation */}
        <Card className={`mt-6 ${theme === 'retro' ? "border-2 border-black rounded-2xl shadow-[4px_4px_0_0_rgba(0,0,0,0.3)]" : ""}`}>
          <CardHeader className="pb-4">
            <CardTitle className="text-lg">🎮 Servo Movement Simulator</CardTitle>
          </CardHeader>
          <CardContent className="space-y-6">
            {/* Pattern Selector */}
            <div className="grid grid-cols-3 gap-2">
              {(Object.keys(servoPatterns) as Array<keyof typeof servoPatterns>).map((key) => (
                <Button
                  key={key}
                  variant={selectedPattern === key ? "default" : "outline"}
                  size="sm"
                  onClick={() => {
                    if (isPlaying) stopSimulation();
                    setSelectedPattern(key);
                    resetSimulation();
                  }}
                  className="text-xs"
                >
                  {servoPatterns[key].name}
                </Button>
              ))}
            </div>

            {/* Pattern Info */}
            <div className="text-sm bg-muted p-3 rounded-lg">
              <div className="flex justify-between items-center">
                <div className="font-medium text-foreground">{servoPatterns[selectedPattern].name}</div>
                <div className="text-xs text-muted-foreground">
                  {servoPatterns[selectedPattern].totalFrames} frames @ {servoPatterns[selectedPattern].fps}fps
                </div>
              </div>
              <div className="text-muted-foreground text-xs mt-1">{servoPatterns[selectedPattern].description}</div>
            </div>

            {/* Servo Visualization */}
            <div className="relative h-40 bg-gradient-to-b from-slate-100 to-slate-200 dark:from-slate-800 dark:to-slate-900 rounded-xl overflow-hidden">
              {/* Frame & Angle display */}
              <div className="absolute top-2 left-2 bg-black/60 text-white text-xs px-2 py-1 rounded font-mono">
                Frame: {frame}/{servoPatterns[selectedPattern].totalFrames}
              </div>
              <div className="absolute top-2 right-2 bg-black/60 text-white text-xs px-2 py-1 rounded font-mono">
                {servoAngle}°
              </div>

              {/* Playing indicator */}
              {isPlaying && (
                <div className="absolute top-2 left-1/2 -translate-x-1/2 bg-green-500 text-white text-xs px-2 py-1 rounded animate-pulse">
                  ● PLAYING
                </div>
              )}

              {/* Servo visualization */}
              <div className="absolute inset-x-0 bottom-6 flex justify-center">
                <div className="relative w-56 h-28">
                  {/* Degree markers */}
                  <div className="absolute left-0 bottom-0 text-xs text-muted-foreground font-mono">15°</div>
                  <div className="absolute left-1/2 -translate-x-1/2 bottom-0 text-xs text-muted-foreground font-mono">90°</div>
                  <div className="absolute right-0 bottom-0 text-xs text-muted-foreground font-mono">165°</div>
                  
                  {/* Arc guide */}
                  <div className="absolute left-1/2 bottom-0 w-40 h-20 -translate-x-1/2 border-t-2 border-dashed border-slate-400/30 rounded-t-full" />
                  
                  {/* Servo arm - CSS transition for smooth movement */}
                  <div 
                    className="absolute left-1/2 bottom-0 w-1.5 h-24 bg-gradient-to-t from-orange-600 via-orange-500 to-orange-400 rounded-full origin-bottom shadow-lg"
                    style={{
                      transform: `translateX(-50%) rotate(${servoAngle - 90}deg)`,
                      transition: 'transform 150ms ease-out',
                    }}
                  >
                    {/* Servo head */}
                    <div className="absolute -top-4 left-1/2 -translate-x-1/2 w-8 h-8 bg-slate-700 rounded-full border-2 border-slate-500 flex items-center justify-center shadow-md">
                      <div className={`w-3 h-3 rounded-full ${isPlaying ? 'bg-green-400 animate-pulse' : 'bg-slate-500'}`} />
                    </div>
                  </div>
                  
                  {/* Servo base */}
                  <div className="absolute left-1/2 -translate-x-1/2 bottom-0 w-10 h-5 bg-slate-600 rounded-t-lg shadow-md" />
                </div>
              </div>
            </div>

            {/* Keyframe Timeline */}
            <div className="space-y-3">
              <div className="flex justify-between items-center">
                <div className="text-xs font-medium text-muted-foreground">Timeline</div>
                <div className="text-xs text-muted-foreground">
                  {servoPatterns[selectedPattern].keyframes.length} keyframes
                </div>
              </div>
              
              {/* Progress bar with keyframe markers */}
              <div className="relative h-10 bg-muted rounded-lg overflow-hidden">
                {/* Progress fill */}
                <div 
                  className="absolute inset-y-0 left-0 bg-primary/30"
                  style={{ width: `${(frame / servoPatterns[selectedPattern].totalFrames) * 100}%` }}
                />
                
                {/* Playhead */}
                <div 
                  className="absolute top-0 bottom-0 w-0.5 bg-primary z-10"
                  style={{ left: `${(frame / servoPatterns[selectedPattern].totalFrames) * 100}%` }}
                />
                
                {/* Keyframe markers */}
                {servoPatterns[selectedPattern].keyframes.map((kf, i) => (
                  <div
                    key={i}
                    className={`absolute top-1/2 -translate-y-1/2 w-4 h-4 rounded-full border-2 cursor-pointer
                      ${activeKeyframeIndex === i 
                        ? 'bg-primary border-primary scale-125' 
                        : frame >= kf.frame 
                          ? 'bg-primary/60 border-primary/60' 
                          : 'bg-background border-muted-foreground/50'
                      }`}
                    style={{ left: `calc(${(kf.frame / servoPatterns[selectedPattern].totalFrames) * 100}% - 8px)` }}
                    title={`Frame ${kf.frame}: ${kf.label} (${kf.position}°)`}
                    onClick={() => {
                      // Jump to this keyframe
                      setFrame(kf.frame);
                      setServoAngle(kf.position);
                      setActiveKeyframeIndex(i);
                    }}
                  />
                ))}
              </div>
              
              {/* Keyframe labels */}
              <div className="flex flex-wrap gap-1.5">
                {servoPatterns[selectedPattern].keyframes.map((kf, i) => (
                  <button 
                    key={i}
                    onClick={() => {
                      setFrame(kf.frame);
                      setServoAngle(kf.position);
                      setActiveKeyframeIndex(i);
                      // Also send to Tabbie if connected
                      if (isConnected) {
                        sendServoPosition(kf.position);
                      }
                    }}
                    className={`px-2 py-1 rounded text-xs transition-colors
                      ${activeKeyframeIndex === i 
                        ? 'bg-primary text-primary-foreground ring-2 ring-primary ring-offset-1' 
                        : frame >= kf.frame 
                          ? 'bg-primary/20 text-foreground' 
                          : 'bg-muted text-muted-foreground hover:bg-muted/80'
                      }`}
                  >
                    F{kf.frame}: {kf.label} ({kf.position}°)
                  </button>
                ))}
              </div>
            </div>

            {/* Controls */}
            <div className="flex gap-2">
              <Button
                onClick={isPlaying ? stopSimulation : playAndSendToTabbie}
                variant={isPlaying ? "destructive" : "default"}
                className="flex-1"
              >
                {isPlaying ? (
                  <>
                    <Square className="h-4 w-4 mr-2" />
                    Stop
                  </>
                ) : (
                  <>
                    <Play className="h-4 w-4 mr-2" />
                    {isConnected ? 'Play & Send to Tabbie' : 'Play Preview'}
                  </>
                )}
              </Button>
              <Button
                onClick={resetSimulation}
                variant="outline"
                disabled={isPlaying}
                title="Reset"
              >
                <RotateCcw className="h-4 w-4" />
              </Button>
            </div>
            
            {/* Connection status */}
            <div className={`text-xs text-center py-2 px-3 rounded ${isConnected ? 'bg-green-500/10 text-green-600' : 'bg-muted text-muted-foreground'}`}>
              {isConnected 
                ? `✓ Connected to Tabbie (${customIP}) - animations will sync` 
                : 'Not connected - preview only mode'}
            </div>

            {/* Direct Servo Control */}
            {isConnected && (
              <div className="pt-4 border-t space-y-3">
                <div className="text-sm font-medium">Direct Servo Control</div>
                <div className="flex gap-2">
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    onClick={() => sendServoPosition('left')}
                  >
                    ← Left ({SERVO_LEFT}°)
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    onClick={() => sendServoPosition('center')}
                  >
                    Center ({SERVO_CENTER}°)
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    onClick={() => sendServoPosition('right')}
                  >
                    Right ({SERVO_RIGHT}°) →
                  </Button>
                </div>
              </div>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
};

export default TabbiePage;
