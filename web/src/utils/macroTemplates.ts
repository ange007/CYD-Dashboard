import type { Macro } from '../stores/macros';

export interface MacroTemplate {
  name: string;
  description: string;
  category: string;
  icon: string;
  macro: Omit<Macro, 'id'>;
}

export const MACRO_TEMPLATES: MacroTemplate[] = [
  // ── Media ────────────────────────────────────────────────────────────────
  {
    name: 'Play / Pause',
    description: 'Toggle media playback',
    category: 'Media',
    icon: '▶️',
    macro: { title: 'Play/Pause', icon: 'PLAY', type: 'keys', keys: ['KEY_MEDIA_PLAY_PAUSE'] },
  },
  {
    name: 'Next track',
    description: 'Skip to the next media track',
    category: 'Media',
    icon: '⏭️',
    macro: { title: 'Next', icon: 'NEXT', type: 'keys', keys: ['KEY_MEDIA_NEXT_TRACK'] },
  },
  {
    name: 'Previous track',
    description: 'Go back to the previous track',
    category: 'Media',
    icon: '⏮️',
    macro: { title: 'Prev', icon: 'PREV', type: 'keys', keys: ['KEY_MEDIA_PREVIOUS_TRACK'] },
  },
  {
    name: 'Volume up',
    description: 'Increase system volume',
    category: 'Media',
    icon: '🔊',
    macro: { title: 'Vol +', icon: 'VOLUME_MAX', type: 'keys', keys: ['KEY_MEDIA_VOLUME_UP'] },
  },
  {
    name: 'Volume down',
    description: 'Decrease system volume',
    category: 'Media',
    icon: '🔉',
    macro: { title: 'Vol −', icon: 'VOLUME_MID', type: 'keys', keys: ['KEY_MEDIA_VOLUME_DOWN'] },
  },
  {
    name: 'Mute',
    description: 'Toggle audio mute',
    category: 'Media',
    icon: '🔇',
    macro: { title: 'Mute', icon: 'MUTE', type: 'keys', keys: ['KEY_MEDIA_MUTE'] },
  },
  {
    name: 'Stop',
    description: 'Stop media playback',
    category: 'Media',
    icon: '⏹️',
    macro: { title: 'Stop', icon: 'STOP', type: 'keys', keys: ['KEY_MEDIA_STOP'] },
  },

  // ── Browser ───────────────────────────────────────────────────────────────
  {
    name: 'New tab',
    description: 'Open a new browser tab',
    category: 'Browser',
    icon: '🗂️',
    macro: { title: 'New tab', icon: 'PLUS', type: 'keys', keys: ['KEY_LEFT_CTRL+t'] },
  },
  {
    name: 'Close tab',
    description: 'Close the current browser tab',
    category: 'Browser',
    icon: '✖️',
    macro: { title: 'Close tab', icon: 'CLOSE', type: 'keys', keys: ['KEY_LEFT_CTRL+w'] },
  },
  {
    name: 'Reopen tab',
    description: 'Reopen the last closed tab',
    category: 'Browser',
    icon: '↩️',
    macro: { title: 'Reopen', icon: 'REFRESH', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+t'] },
  },
  {
    name: 'Reload page',
    description: 'Reload the current page',
    category: 'Browser',
    icon: '🔄',
    macro: { title: 'Reload', icon: 'REFRESH', type: 'keys', keys: ['KEY_F5'] },
  },
  {
    name: 'Go back',
    description: 'Navigate back in browser history',
    category: 'Browser',
    icon: '◀️',
    macro: { title: 'Back', icon: 'LEFT', type: 'keys', keys: ['KEY_LEFT_ALT+KEY_LEFT_ARROW'] },
  },
  {
    name: 'Go forward',
    description: 'Navigate forward in browser history',
    category: 'Browser',
    icon: '▶️',
    macro: { title: 'Forward', icon: 'RIGHT', type: 'keys', keys: ['KEY_LEFT_ALT+KEY_RIGHT_ARROW'] },
  },
  {
    name: 'Incognito window',
    description: 'Open a new incognito / private window',
    category: 'Browser',
    icon: '🕵️',
    macro: { title: 'Incognito', icon: 'EYE_CLOSE', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+n'] },
  },
  {
    name: 'Address bar',
    description: 'Focus the browser address bar',
    category: 'Browser',
    icon: '🔗',
    macro: { title: 'URL bar', icon: 'WIFI', type: 'keys', keys: ['KEY_LEFT_CTRL+l'] },
  },

  // ── System ────────────────────────────────────────────────────────────────
  {
    name: 'Screenshot',
    description: 'Take a screenshot (Print Screen)',
    category: 'System',
    icon: '📸',
    macro: { title: 'Screenshot', icon: 'IMAGE', type: 'keys', keys: ['KEY_PRTSC'] },
  },
  {
    name: 'Lock screen',
    description: 'Lock the workstation (Win+L)',
    category: 'System',
    icon: '🔒',
    macro: { title: 'Lock', icon: 'CLOSE', type: 'keys', keys: ['KEY_LEFT_GUI+l'] },
  },
  {
    name: 'Show desktop',
    description: 'Minimize all windows (Win+D)',
    category: 'System',
    icon: '🖥️',
    macro: { title: 'Desktop', icon: 'HOME', type: 'keys', keys: ['KEY_LEFT_GUI+d'] },
  },
  {
    name: 'Task manager',
    description: 'Open Task Manager (Ctrl+Shift+Esc)',
    category: 'System',
    icon: '📊',
    macro: { title: 'Tasks', icon: 'LIST', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+KEY_ESC'] },
  },
  {
    name: 'File Explorer',
    description: 'Open Windows File Explorer (Win+E)',
    category: 'System',
    icon: '📁',
    macro: { title: 'Explorer', icon: 'DIRECTORY', type: 'keys', keys: ['KEY_LEFT_GUI+e'] },
  },
  {
    name: 'Run dialog',
    description: 'Open the Run dialog (Win+R)',
    category: 'System',
    icon: '⚙️',
    macro: { title: 'Run', icon: 'SETTINGS', type: 'keys', keys: ['KEY_LEFT_GUI+r'] },
  },
  {
    name: 'Sleep',
    description: 'Put the computer to sleep',
    category: 'System',
    icon: '😴',
    macro: { title: 'Sleep', icon: 'POWER', type: 'command', cmd: 'rundll32.exe powrprof.dll,SetSuspendState 0,1,0' },
  },

  // ── Editing ───────────────────────────────────────────────────────────────
  {
    name: 'Copy',
    description: 'Copy selection to clipboard (Ctrl+C)',
    category: 'Editing',
    icon: '📋',
    macro: { title: 'Copy', icon: 'COPY', type: 'keys', keys: ['KEY_LEFT_CTRL+c'] },
  },
  {
    name: 'Paste',
    description: 'Paste from clipboard (Ctrl+V)',
    category: 'Editing',
    icon: '📌',
    macro: { title: 'Paste', icon: 'PASTE', type: 'keys', keys: ['KEY_LEFT_CTRL+v'] },
  },
  {
    name: 'Cut',
    description: 'Cut selection to clipboard (Ctrl+X)',
    category: 'Editing',
    icon: '✂️',
    macro: { title: 'Cut', icon: 'CUT', type: 'keys', keys: ['KEY_LEFT_CTRL+x'] },
  },
  {
    name: 'Undo',
    description: 'Undo last action (Ctrl+Z)',
    category: 'Editing',
    icon: '↩️',
    macro: { title: 'Undo', icon: 'LEFT', type: 'keys', keys: ['KEY_LEFT_CTRL+z'] },
  },
  {
    name: 'Redo',
    description: 'Redo last undone action (Ctrl+Y)',
    category: 'Editing',
    icon: '↪️',
    macro: { title: 'Redo', icon: 'RIGHT', type: 'keys', keys: ['KEY_LEFT_CTRL+y'] },
  },
  {
    name: 'Select all',
    description: 'Select all content (Ctrl+A)',
    category: 'Editing',
    icon: '🔲',
    macro: { title: 'Select all', icon: 'OK', type: 'keys', keys: ['KEY_LEFT_CTRL+a'] },
  },
  {
    name: 'Save',
    description: 'Save the current file (Ctrl+S)',
    category: 'Editing',
    icon: '💾',
    macro: { title: 'Save', icon: 'SAVE', type: 'keys', keys: ['KEY_LEFT_CTRL+s'] },
  },
  {
    name: 'Save as',
    description: 'Save as a new file (Ctrl+Shift+S)',
    category: 'Editing',
    icon: '📝',
    macro: { title: 'Save as', icon: 'SAVE', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+s'] },
  },
  {
    name: 'Find',
    description: 'Open find/search dialog (Ctrl+F)',
    category: 'Editing',
    icon: '🔍',
    macro: { title: 'Find', icon: 'FILE', type: 'keys', keys: ['KEY_LEFT_CTRL+f'] },
  },
  {
    name: 'Find & Replace',
    description: 'Open find & replace dialog (Ctrl+H)',
    category: 'Editing',
    icon: '🔄',
    macro: { title: 'Replace', icon: 'REFRESH', type: 'keys', keys: ['KEY_LEFT_CTRL+h'] },
  },

  // ── Window ────────────────────────────────────────────────────────────────
  {
    name: 'Switch window (Alt+Tab)',
    description: 'Open the window switcher',
    category: 'Window',
    icon: '🪟',
    macro: { title: 'Alt+Tab', icon: 'LIST', type: 'keys', keys: ['KEY_LEFT_ALT+KEY_TAB'] },
  },
  {
    name: 'Minimize window',
    description: 'Minimize the current window (Win+↓)',
    category: 'Window',
    icon: '⬇️',
    macro: { title: 'Minimize', icon: 'DOWN', type: 'keys', keys: ['KEY_LEFT_GUI+KEY_DOWN_ARROW'] },
  },
  {
    name: 'Maximize window',
    description: 'Maximize the current window (Win+↑)',
    category: 'Window',
    icon: '⬆️',
    macro: { title: 'Maximize', icon: 'UP', type: 'keys', keys: ['KEY_LEFT_GUI+KEY_UP_ARROW'] },
  },
  {
    name: 'Close window',
    description: 'Close the current window (Alt+F4)',
    category: 'Window',
    icon: '❌',
    macro: { title: 'Close', icon: 'CLOSE', type: 'keys', keys: ['KEY_LEFT_ALT+KEY_F4'] },
  },
  {
    name: 'Snap left',
    description: 'Snap window to left half (Win+←)',
    category: 'Window',
    icon: '◀️',
    macro: { title: 'Snap ←', icon: 'LEFT', type: 'keys', keys: ['KEY_LEFT_GUI+KEY_LEFT_ARROW'] },
  },
  {
    name: 'Snap right',
    description: 'Snap window to right half (Win+→)',
    category: 'Window',
    icon: '▶️',
    macro: { title: 'Snap →', icon: 'RIGHT', type: 'keys', keys: ['KEY_LEFT_GUI+KEY_RIGHT_ARROW'] },
  },

  // ── Office / Apps ─────────────────────────────────────────────────────────
  {
    name: 'Bold',
    description: 'Toggle bold formatting (Ctrl+B)',
    category: 'Office',
    icon: '𝐁',
    macro: { title: 'Bold', icon: 'EDIT', type: 'keys', keys: ['KEY_LEFT_CTRL+b'] },
  },
  {
    name: 'Italic',
    description: 'Toggle italic formatting (Ctrl+I)',
    category: 'Office',
    icon: '𝘐',
    macro: { title: 'Italic', icon: 'EDIT', type: 'keys', keys: ['KEY_LEFT_CTRL+i'] },
  },
  {
    name: 'Underline',
    description: 'Toggle underline formatting (Ctrl+U)',
    category: 'Office',
    icon: '͟U',
    macro: { title: 'Underline', icon: 'EDIT', type: 'keys', keys: ['KEY_LEFT_CTRL+u'] },
  },
  {
    name: 'Print',
    description: 'Open print dialog (Ctrl+P)',
    category: 'Office',
    icon: '🖨️',
    macro: { title: 'Print', icon: 'DOWNLOAD', type: 'keys', keys: ['KEY_LEFT_CTRL+p'] },
  },
  {
    name: 'Zoom in',
    description: 'Zoom in (Ctrl++)',
    category: 'Office',
    icon: '🔍',
    macro: { title: 'Zoom +', icon: 'PLUS', type: 'keys', keys: ['KEY_LEFT_CTRL+='] },
  },
  {
    name: 'Zoom out',
    description: 'Zoom out (Ctrl+-)',
    category: 'Office',
    icon: '🔎',
    macro: { title: 'Zoom −', icon: 'MINUS', type: 'keys', keys: ['KEY_LEFT_CTRL+-'] },
  },
  {
    name: 'New document',
    description: 'Create a new document (Ctrl+N)',
    category: 'Office',
    icon: '📄',
    macro: { title: 'New', icon: 'PLUS', type: 'keys', keys: ['KEY_LEFT_CTRL+n'] },
  },

  // ── VS Code ──────────────────────────────────────────────────────────────
  {
    name: 'Toggle terminal',
    description: 'Open/close integrated terminal (Ctrl+`)',
    category: 'VS Code',
    icon: '🖥️',
    macro: { title: 'Terminal', icon: 'KEYBOARD', type: 'keys', keys: ['KEY_LEFT_CTRL+`'] },
  },
  {
    name: 'Command Palette',
    description: 'Open command palette (Ctrl+Shift+P)',
    category: 'VS Code',
    icon: '⚡',
    macro: { title: 'Commands', icon: 'LIST', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+p'] },
  },
  {
    name: 'Go to File',
    description: 'Quick open file (Ctrl+P)',
    category: 'VS Code',
    icon: '📂',
    macro: { title: 'Go to File', icon: 'FILE', type: 'keys', keys: ['KEY_LEFT_CTRL+p'] },
  },
  {
    name: 'Toggle sidebar',
    description: 'Show/hide the sidebar (Ctrl+B)',
    category: 'VS Code',
    icon: '📑',
    macro: { title: 'Sidebar', icon: 'LIST', type: 'keys', keys: ['KEY_LEFT_CTRL+b'] },
  },
  {
    name: 'Format document',
    description: 'Auto-format code (Shift+Alt+F)',
    category: 'VS Code',
    icon: '✨',
    macro: { title: 'Format', icon: 'EDIT', type: 'keys', keys: ['KEY_LEFT_SHIFT+KEY_LEFT_ALT+f'] },
  },
  {
    name: 'Toggle comment',
    description: 'Comment/uncomment line (Ctrl+/)',
    category: 'VS Code',
    icon: '💬',
    macro: { title: 'Comment', icon: 'EDIT', type: 'keys', keys: ['KEY_LEFT_CTRL+/'] },
  },

  // ── Adobe Photoshop ──────────────────────────────────────────────────────
  {
    name: 'New layer',
    description: 'Create a new layer (Ctrl+Shift+N)',
    category: 'Photoshop',
    icon: '🖼️',
    macro: { title: 'New Layer', icon: 'PLUS', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+n'] },
  },
  {
    name: 'Brush tool',
    description: 'Select brush tool (B)',
    category: 'Photoshop',
    icon: '🖌️',
    macro: { title: 'Brush', icon: 'EDIT', type: 'keys', keys: ['b'] },
  },
  {
    name: 'Free Transform',
    description: 'Enter free transform mode (Ctrl+T)',
    category: 'Photoshop',
    icon: '🔄',
    macro: { title: 'Transform', icon: 'REFRESH', type: 'keys', keys: ['KEY_LEFT_CTRL+t'] },
  },
  {
    name: 'Deselect',
    description: 'Remove selection (Ctrl+D)',
    category: 'Photoshop',
    icon: '⬜',
    macro: { title: 'Deselect', icon: 'CLOSE', type: 'keys', keys: ['KEY_LEFT_CTRL+d'] },
  },
  {
    name: 'Fit on screen',
    description: 'Zoom to fit (Ctrl+0)',
    category: 'Photoshop',
    icon: '🔍',
    macro: { title: 'Fit', icon: 'EYE_OPEN', type: 'keys', keys: ['KEY_LEFT_CTRL+0'] },
  },
  {
    name: 'Flatten image',
    description: 'Merge all visible layers',
    category: 'Photoshop',
    icon: '📋',
    macro: { title: 'Flatten', icon: 'LIST', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+e'] },
  },

  // ── Adobe Premiere ───────────────────────────────────────────────────────
  {
    name: 'Razor tool',
    description: 'Select the razor tool (C)',
    category: 'Premiere',
    icon: '🔪',
    macro: { title: 'Razor', icon: 'CUT', type: 'keys', keys: ['c'] },
  },
  {
    name: 'Ripple delete',
    description: 'Delete clip and close gap (Shift+Del)',
    category: 'Premiere',
    icon: '🗑️',
    macro: { title: 'Ripple Del', icon: 'TRASH', type: 'keys', keys: ['KEY_LEFT_SHIFT+KEY_DELETE'] },
  },
  {
    name: 'Mark In',
    description: 'Set In point (I)',
    category: 'Premiere',
    icon: '▶️',
    macro: { title: 'In', icon: 'LEFT', type: 'keys', keys: ['i'] },
  },
  {
    name: 'Mark Out',
    description: 'Set Out point (O)',
    category: 'Premiere',
    icon: '⏹️',
    macro: { title: 'Out', icon: 'RIGHT', type: 'keys', keys: ['o'] },
  },
  {
    name: 'Render',
    description: 'Render timeline (Enter)',
    category: 'Premiere',
    icon: '⚙️',
    macro: { title: 'Render', icon: 'SETTINGS', type: 'keys', keys: ['KEY_RETURN'] },
  },

  // ── Blender ──────────────────────────────────────────────────────────────
  {
    name: 'Toggle edit mode',
    description: 'Switch between Object and Edit mode (Tab)',
    category: 'Blender',
    icon: '🧊',
    macro: { title: 'Tab', icon: 'REFRESH', type: 'keys', keys: ['KEY_TAB'] },
  },
  {
    name: 'Extrude',
    description: 'Extrude selection (E)',
    category: 'Blender',
    icon: '📐',
    macro: { title: 'Extrude', icon: 'UP', type: 'keys', keys: ['e'] },
  },
  {
    name: 'Scale',
    description: 'Scale selection (S)',
    category: 'Blender',
    icon: '↔️',
    macro: { title: 'Scale', icon: 'SETTINGS', type: 'keys', keys: ['s'] },
  },
  {
    name: 'Grab / Move',
    description: 'Move selection (G)',
    category: 'Blender',
    icon: '✋',
    macro: { title: 'Grab', icon: 'SETTINGS', type: 'keys', keys: ['g'] },
  },
  {
    name: 'Render image',
    description: 'Render current frame (F12)',
    category: 'Blender',
    icon: '📸',
    macro: { title: 'Render', icon: 'IMAGE', type: 'keys', keys: ['KEY_F12'] },
  },

  // ── OBS Studio ───────────────────────────────────────────────────────────
  {
    name: 'Start/Stop Streaming',
    description: 'Toggle streaming (customise hotkey in OBS)',
    category: 'OBS Studio',
    icon: '📡',
    macro: { title: 'Stream', icon: 'WIFI', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+KEY_F11'] },
  },
  {
    name: 'Start/Stop Recording',
    description: 'Toggle recording (customise hotkey in OBS)',
    category: 'OBS Studio',
    icon: '⏺️',
    macro: { title: 'Record', icon: 'PLAY', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+KEY_F12'] },
  },
  {
    name: 'Switch scene 1',
    description: 'Switch to scene 1 (customise in OBS)',
    category: 'OBS Studio',
    icon: '1️⃣',
    macro: { title: 'Scene 1', icon: 'LIST', type: 'keys', keys: ['KEY_F13'] },
  },
  {
    name: 'Toggle mute source',
    description: 'Mute/unmute audio source (customise in OBS)',
    category: 'OBS Studio',
    icon: '🔇',
    macro: { title: 'Mute', icon: 'MUTE', type: 'keys', keys: ['KEY_F14'] },
  },

  // ── FL Studio / DAW ──────────────────────────────────────────────────────
  {
    name: 'Play / Stop',
    description: 'Toggle playback (Space)',
    category: 'DAW',
    icon: '🎵',
    macro: { title: 'Play', icon: 'PLAY', type: 'keys', keys: [' '] },
  },
  {
    name: 'Record',
    description: 'Toggle recording (R in FL Studio)',
    category: 'DAW',
    icon: '⏺️',
    macro: { title: 'Record', icon: 'PLAY', type: 'keys', keys: ['r'] },
  },
  {
    name: 'Piano Roll',
    description: 'Open Piano Roll (F7 in FL Studio)',
    category: 'DAW',
    icon: '🎹',
    macro: { title: 'Piano Roll', icon: 'KEYBOARD', type: 'keys', keys: ['KEY_F7'] },
  },
  {
    name: 'Mixer',
    description: 'Open Mixer (F9 in FL Studio)',
    category: 'DAW',
    icon: '🎛️',
    macro: { title: 'Mixer', icon: 'SETTINGS', type: 'keys', keys: ['KEY_F9'] },
  },

  // ── DaVinci Resolve ──────────────────────────────────────────────────────
  {
    name: 'Blade tool',
    description: 'Select blade/split tool (B)',
    category: 'DaVinci Resolve',
    icon: '🔪',
    macro: { title: 'Blade', icon: 'CUT', type: 'keys', keys: ['b'] },
  },
  {
    name: 'Trim tool',
    description: 'Select trim edit mode (T)',
    category: 'DaVinci Resolve',
    icon: '✂️',
    macro: { title: 'Trim', icon: 'CUT', type: 'keys', keys: ['t'] },
  },
  {
    name: 'Color page',
    description: 'Switch to Color page (Shift+6)',
    category: 'DaVinci Resolve',
    icon: '🎨',
    macro: { title: 'Color', icon: 'SETTINGS', type: 'keys', keys: ['KEY_LEFT_SHIFT+6'] },
  },
  {
    name: 'Deliver page',
    description: 'Switch to Deliver page (Shift+8)',
    category: 'DaVinci Resolve',
    icon: '📤',
    macro: { title: 'Deliver', icon: 'UPLOAD', type: 'keys', keys: ['KEY_LEFT_SHIFT+8'] },
  },

  // ── Discord ──────────────────────────────────────────────────────────────
  {
    name: 'Toggle mute',
    description: 'Mute/unmute mic in Discord (Ctrl+Shift+M)',
    category: 'Discord',
    icon: '🎤',
    macro: { title: 'Mute', icon: 'MUTE', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+m'] },
  },
  {
    name: 'Toggle deafen',
    description: 'Deafen/undeafen in Discord (Ctrl+Shift+D)',
    category: 'Discord',
    icon: '🔇',
    macro: { title: 'Deafen', icon: 'VOLUME_MID', type: 'keys', keys: ['KEY_LEFT_CTRL+KEY_LEFT_SHIFT+d'] },
  },
  {
    name: 'Toggle overlay',
    description: 'Show/hide Discord overlay (Shift+`)',
    category: 'Discord',
    icon: '🎮',
    macro: { title: 'Overlay', icon: 'EYE_OPEN', type: 'keys', keys: ['KEY_LEFT_SHIFT+`'] },
  },

  // ── Spotify ──────────────────────────────────────────────────────────────
  {
    name: 'Like song',
    description: 'Add current track to Liked (Alt+Shift+B)',
    category: 'Spotify',
    icon: '💚',
    macro: { title: 'Like', icon: 'OK', type: 'keys', keys: ['KEY_LEFT_ALT+KEY_LEFT_SHIFT+b'] },
  },
  {
    name: 'Toggle shuffle',
    description: 'Toggle shuffle mode (Ctrl+S in Spotify)',
    category: 'Spotify',
    icon: '🔀',
    macro: { title: 'Shuffle', icon: 'REFRESH', type: 'keys', keys: ['KEY_LEFT_CTRL+s'] },
  },
  {
    name: 'Toggle repeat',
    description: 'Cycle repeat mode (Ctrl+R in Spotify)',
    category: 'Spotify',
    icon: '🔁',
    macro: { title: 'Repeat', icon: 'REFRESH', type: 'keys', keys: ['KEY_LEFT_CTRL+r'] },
  },

  // ── VLC ──────────────────────────────────────────────────────────────────
  {
    name: 'Toggle subtitles',
    description: 'Show/hide subtitles (V)',
    category: 'VLC',
    icon: '💬',
    macro: { title: 'Subtitles', icon: 'LIST', type: 'keys', keys: ['v'] },
  },
  {
    name: 'Cycle audio track',
    description: 'Switch audio track (B)',
    category: 'VLC',
    icon: '🔊',
    macro: { title: 'Audio', icon: 'VOLUME_MAX', type: 'keys', keys: ['b'] },
  },
  {
    name: 'Fullscreen',
    description: 'Toggle fullscreen (F)',
    category: 'VLC',
    icon: '🖥️',
    macro: { title: 'Fullscreen', icon: 'IMAGE', type: 'keys', keys: ['f'] },
  },
];

export const MACRO_TEMPLATE_CATEGORIES = [...new Set(MACRO_TEMPLATES.map(t => t.category))];
