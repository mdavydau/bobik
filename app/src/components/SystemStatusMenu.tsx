import React from "react"
import { Bot, CheckCircle2, ChevronDown, Globe2, Radio, Wifi, XCircle } from "lucide-react"
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { useTabbieSync } from "@/contexts/TabbieContext"
import { cn } from "@/lib/utils"

type Tone = "ok" | "warn" | "off"

const toneClass: Record<Tone, string> = {
  ok: "text-emerald-600 dark:text-emerald-400",
  warn: "text-amber-600 dark:text-amber-400",
  off: "text-red-600 dark:text-red-400",
}

const dotClass: Record<Tone, string> = {
  ok: "bg-emerald-500",
  warn: "bg-amber-500",
  off: "bg-red-500",
}

function StatusPill({
  label,
  tone,
  children,
}: {
  label: string
  tone: Tone
  children: React.ReactNode
}) {
  return (
    <Tooltip>
      <TooltipTrigger asChild>
        <span
          className={cn(
            "inline-flex h-8 min-w-8 items-center justify-center gap-1 rounded-md border bg-background px-2 text-[10px] font-semibold leading-none shadow-sm",
            toneClass[tone]
          )}
          aria-label={label}
        >
          {children}
        </span>
      </TooltipTrigger>
      <TooltipContent side="bottom">{label}</TooltipContent>
    </Tooltip>
  )
}

function StatusRow({
  icon,
  label,
  value,
  tone,
}: {
  icon: React.ReactNode
  label: string
  value: string
  tone: Tone
}) {
  return (
    <div className="flex items-center gap-3 px-2 py-2 text-sm">
      <span className={cn("flex h-8 w-8 items-center justify-center rounded-md bg-muted", toneClass[tone])}>
        {icon}
      </span>
      <span className="min-w-0 flex-1">
        <span className="block font-medium leading-5">{label}</span>
        <span className="block truncate text-xs text-muted-foreground">{value}</span>
      </span>
      <span className={cn("h-2.5 w-2.5 rounded-full", dotClass[tone])} />
    </div>
  )
}

export default function SystemStatusMenu() {
  const { isConnected, isConnecting, tabbieStatus, customIP, checkConnection } = useTabbieSync()
  const [isOnline, setIsOnline] = React.useState(() => navigator.onLine)

  React.useEffect(() => {
    const updateOnline = () => setIsOnline(navigator.onLine)
    window.addEventListener("online", updateOnline)
    window.addEventListener("offline", updateOnline)
    return () => {
      window.removeEventListener("online", updateOnline)
      window.removeEventListener("offline", updateOnline)
    }
  }, [])

  const tabbieTone: Tone = isConnected ? "ok" : isConnecting ? "warn" : "off"
  const mqttKnown = typeof tabbieStatus?.mqttConnected === "boolean"
  const mqttTone: Tone = !isConnected || !tabbieStatus?.mqttEnabled ? "off" : mqttKnown && tabbieStatus.mqttConnected ? "ok" : "warn"
  const wifiTone: Tone = isConnected && tabbieStatus?.status === "connected" ? "ok" : isConnecting ? "warn" : "off"

  const mqttValue = !isConnected
    ? "Tabbie offline"
    : !tabbieStatus?.mqttEnabled
      ? "Not enabled in firmware"
      : tabbieStatus.mqttConnected
        ? "Connected"
        : `Disconnected${typeof tabbieStatus.mqttState === "number" ? ` (state ${tabbieStatus.mqttState})` : ""}`

  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <button
          type="button"
          className="mr-4 inline-flex h-10 items-center gap-1 rounded-md px-1.5 outline-none transition-colors hover:bg-accent focus-visible:ring-2 focus-visible:ring-ring"
          aria-label="System status"
        >
          <StatusPill label={isOnline ? "Internet online" : "Internet offline"} tone={isOnline ? "ok" : "off"}>
            <Globe2 className="h-3.5 w-3.5" />
            <span className="hidden sm:inline">NET</span>
          </StatusPill>
          <StatusPill label={isConnected ? "Tabbie connected" : "Tabbie disconnected"} tone={tabbieTone}>
            <Bot className="h-3.5 w-3.5" />
            <span className="hidden sm:inline">BOT</span>
          </StatusPill>
          <StatusPill label={mqttValue} tone={mqttTone}>
            <Radio className="h-3.5 w-3.5" />
            <span className="hidden sm:inline">MQTT</span>
          </StatusPill>
          <ChevronDown className="ml-1 h-3.5 w-3.5 text-muted-foreground" />
        </button>
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end" className="w-72 p-2">
        <DropdownMenuLabel className="flex items-center justify-between px-2">
          <span>System status</span>
          {isOnline && isConnected && tabbieStatus?.mqttConnected ? (
            <CheckCircle2 className="h-4 w-4 text-emerald-500" />
          ) : (
            <XCircle className="h-4 w-4 text-amber-500" />
          )}
        </DropdownMenuLabel>
        <DropdownMenuSeparator />
        <StatusRow
          icon={<Globe2 className="h-4 w-4" />}
          label="Internet"
          value={isOnline ? "Browser online" : "Browser offline"}
          tone={isOnline ? "ok" : "off"}
        />
        <StatusRow
          icon={<Bot className="h-4 w-4" />}
          label="Tabbie"
          value={isConnected ? `${tabbieStatus?.ip || customIP} · ${tabbieStatus?.animation || "idle"}` : "Not reachable"}
          tone={tabbieTone}
        />
        <StatusRow
          icon={<Radio className="h-4 w-4" />}
          label="MQTT"
          value={mqttValue}
          tone={mqttTone}
        />
        <StatusRow
          icon={<Wifi className="h-4 w-4" />}
          label="WiFi"
          value={tabbieStatus?.ssid ? `${tabbieStatus.ssid}${typeof tabbieStatus.rssi === "number" ? ` · ${tabbieStatus.rssi} dBm` : ""}` : "No board status"}
          tone={wifiTone}
        />
        <DropdownMenuSeparator />
        <button
          type="button"
          onClick={checkConnection}
          className="mt-1 flex h-8 w-full items-center justify-center rounded-md text-xs font-medium text-muted-foreground hover:bg-accent hover:text-accent-foreground"
        >
          Refresh
        </button>
      </DropdownMenuContent>
    </DropdownMenu>
  )
}
