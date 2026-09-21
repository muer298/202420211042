from pathlib import Path
import textwrap
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
from matplotlib import font_manager

OUT = Path(__file__).resolve().parents[1] / 'rendered'
OUT.mkdir(parents=True, exist_ok=True)

# Prefer an installed Chinese-capable font. This keeps the script portable
# across Linux/macOS/Windows without bundling any font files.
def pick_cjk_font():
    linux_noto = Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
    if linux_noto.exists():
        font_manager.fontManager.addfont(str(linux_noto))
    installed = {f.name for f in font_manager.fontManager.ttflist}
    for name in ['Noto Sans CJK SC', 'Noto Sans CJK JP', 'PingFang SC',
                 'Microsoft YaHei', 'SimHei', 'Arial Unicode MS']:
        if name in installed:
            return name
    return 'DejaVu Sans'

CJK_FONT = pick_cjk_font()
plt.rcParams['font.family'] = CJK_FONT
plt.rcParams['axes.unicode_minus'] = False

COLORS = {
    'user_fill': '#ECFEFF', 'user_edge': '#0891B2',
    'kernel_fill': '#EFF6FF', 'kernel_edge': '#2563EB',
    'sched_fill': '#FEF9C3', 'sched_edge': '#CA8A04',
    'device_fill': '#FEF2F2', 'device_edge': '#DC2626',
    'text': '#0F172A', 'line': '#334155', 'guide': '#94A3B8',
    'panel': '#F8FAFC'
}


def save(fig, stem):
    fig.savefig(OUT / f'{stem}.svg', bbox_inches='tight')
    fig.savefig(OUT / f'{stem}.png', dpi=220, bbox_inches='tight')
    plt.close(fig)


def rounded_box(ax, xy, w, h, text, fill, edge, fontsize=10, lw=1.5,
                ha='center', va='center', pad=0.18, zorder=3):
    x, y = xy
    box = FancyBboxPatch((x, y), w, h,
                         boxstyle=f'round,pad={pad},rounding_size=0.08',
                         facecolor=fill, edgecolor=edge, linewidth=lw,
                         zorder=zorder)
    ax.add_patch(box)
    tx = x + w/2 if ha == 'center' else x + 0.15
    ax.text(tx, y + h/2, text, ha=ha, va=va, fontsize=fontsize,
            color=COLORS['text'], zorder=zorder+1, linespacing=1.25)
    return box


def seq_arrow(ax, x1, x2, y, label, dashed=False, fontsize=9.2, label_offset=0.10):
    style = '--' if dashed else '-'
    if abs(x1 - x2) < 1e-6:
        # compact self-call loop
        dx = 0.72
        path = FancyArrowPatch((x1, y), (x1, y-0.48),
                               connectionstyle=f'angle3,angleA=0,angleB=-90',
                               arrowstyle='-|>', mutation_scale=10,
                               linewidth=1.25, linestyle=style,
                               color=COLORS['line'], zorder=4)
        ax.add_patch(path)
        ax.plot([x1, x1+dx], [y, y], color=COLORS['line'], lw=1.25, ls=style, zorder=4)
        ax.plot([x1+dx, x1+dx], [y, y-0.48], color=COLORS['line'], lw=1.25, ls=style, zorder=4)
        ax.plot([x1+dx, x1], [y-0.48, y-0.48], color=COLORS['line'], lw=1.25, ls=style, zorder=4)
        ax.text(x1+0.10, y+label_offset, label, ha='left', va='bottom', fontsize=fontsize,
                color=COLORS['text'], zorder=5)
    else:
        arrow = FancyArrowPatch((x1, y), (x2, y), arrowstyle='-|>', mutation_scale=10,
                                linewidth=1.25, linestyle=style, color=COLORS['line'], zorder=4,
                                shrinkA=0, shrinkB=0)
        ax.add_patch(arrow)
        ax.text((x1+x2)/2, y+label_offset, label, ha='center', va='bottom', fontsize=fontsize,
                color=COLORS['text'], zorder=5)


def participant_box(ax, x, title, kind, y=15.1):
    mapping = {
        'user': (COLORS['user_fill'], COLORS['user_edge']),
        'kernel': (COLORS['kernel_fill'], COLORS['kernel_edge']),
        'sched': (COLORS['sched_fill'], COLORS['sched_edge']),
        'device': (COLORS['device_fill'], COLORS['device_edge']),
    }
    fill, edge = mapping[kind]
    rounded_box(ax, (x-1.05, y), 2.10, 0.82, title, fill, edge, fontsize=10.2)


def draw_flow():
    fig, ax = plt.subplots(figsize=(17, 17))
    ax.set_xlim(0, 16)
    ax.set_ylim(0.2, 16.8)
    ax.axis('off')
    ax.text(0.2, 16.55, '任务1总流程图：read → fork → exec → write → exit',
            fontsize=18, fontweight='bold', ha='left', va='top', color=COLORS['text'])
    ax.text(0.2, 16.15, '标准时序版：事件自上而下，箭头只在对应事件行横向连接，不穿过节点。',
            fontsize=10.5, ha='left', va='top', color='#475569')

    xs = {'SH': 1.5, 'K': 5.0, 'C': 8.3, 'U': 11.5, 'E': 14.5}
    participant_box(ax, xs['SH'], '父进程 sh\n用户态', 'user', y=14.95)
    participant_box(ax, xs['K'], '内核 / 系统调用', 'kernel', y=14.95)
    participant_box(ax, xs['C'], '调度器', 'sched', y=14.95)
    participant_box(ax, xs['U'], 'Console / UART', 'device', y=14.95)
    participant_box(ax, xs['E'], '子进程 echo\n用户态', 'user', y=14.95)

    for x in xs.values():
        ax.plot([x, x], [1.0, 14.95], color=COLORS['guide'], lw=1.0, ls=(0, (3, 4)), zorder=1)

    # Background stage bands
    stages = [
        (10.45, 14.80, '① read 阻塞与唤醒', '#F8FAFC'),
        (6.55, 10.40, '② fork / wait / 调度子进程', '#FCFCFD'),
        (4.80, 6.50, '③ exec', '#F8FAFC'),
        (2.05, 4.75, '④ write 与 UART TX', '#FCFCFD'),
        (0.95, 2.00, '⑤ exit / wait 回收', '#F8FAFC'),
    ]
    for y0, y1, title, fill in stages:
        ax.add_patch(Rectangle((0.25, y0), 15.5, y1-y0, facecolor=fill, edgecolor='none', zorder=0))
        ax.text(0.4, y1-0.14, title, fontsize=9.6, fontweight='bold', color='#64748B', va='top')

    events = [
        (14.60, 'SH','K','read(0, buf) / ecall', False),
        (14.05, 'K','K','保存 trapframe；切换到内核栈', False),
        (13.50, 'K','K','sys_read → consoleread()', False),
        (12.95, 'K','C','无输入：sleep_prepare(&cons.r) → sleep/sched', False),
        (12.35, 'U','K','UART RX 中断（用户输入回车）', True),
        (11.80, 'K','K','uartintr → consoleintr → wakeup(&cons.r)', False),
        (11.25, 'K','C','将 sh 置为 RUNNABLE', False),
        (10.70, 'C','SH','重新调度 sh；read 返回', True),
        (10.10, 'SH','K','fork()', False),
        (9.55, 'K','K','allocproc / copyuvm / copyfd', False),
        (9.00, 'K','SH','父进程返回 child pid', True),
        (8.45, 'K','C','子进程置为 RUNNABLE', False),
        (7.90, 'SH','K','wait()', False),
        (7.35, 'K','C','父进程睡眠，等待子进程退出', False),
        (6.80, 'C','E','调度子进程；fork 返回 0', True),
        (6.20, 'E','K','exec("echo", argv)', False),
        (5.65, 'K','K','kexec 加载 ELF；设置 epc / sp / argv', False),
        (5.10, 'K','E','返回用户态，准备执行 echo 首条指令', True),
        (4.50, 'E','K','write(1, "hi\\n")', False),
        (3.95, 'K','U','sys_write → filewrite → consolewrite → uartwrite', False),
        (3.40, 'K','C','[可选] TX 满：sleep(&uart.tx_chan)', False),
        (2.85, 'U','K','[可选] UART TX 中断', True),
        (2.30, 'K','C','wakeup(&uart.tx_chan)；恢复 echo', False),
        (1.75, 'E','K','exit()', False),
        (1.25, 'K','K','state = ZOMBIE；wakeup(wait)', False),
    ]
    for y, a, b, lab, dashed in events:
        seq_arrow(ax, xs[a], xs[b], y, lab, dashed=dashed)

    # Bottom final return/reap in an inset panel to keep it readable.
    rounded_box(ax, (4.0, 0.30), 8.0, 0.62,
                '调度器恢复父进程 sh → wait() 返回 → 内核回收子进程（freeproc）→ shell 继续运行',
                '#F1F5F9', '#64748B', fontsize=10.2)

    save(fig, 'task1-01-flow_standard')


def draw_snapshot():
    fig, ax = plt.subplots(figsize=(16, 10.5))
    ax.set_xlim(0, 16)
    ax.set_ylim(0, 10.5)
    ax.axis('off')
    ax.text(0.3, 10.2, '任务1快照图：exec 已完成，但 echo 首条用户指令尚未执行',
            fontsize=18, fontweight='bold', color=COLORS['text'], va='top')
    ax.text(0.3, 9.72, '标准结构版：进程关系、地址空间、资源假设分区展示；只保留必要的结构连线。',
            fontsize=10.5, color='#475569', va='top')

    rounded_box(ax, (5.0, 8.72), 6.0, 0.62,
                '快照时刻：exec 已成功替换地址空间，echo 还没有执行首条用户态指令',
                COLORS['sched_fill'], COLORS['sched_edge'], fontsize=10.5)

    ax.text(0.6, 8.25, '进程状态快照', fontsize=12.5, fontweight='bold', color=COLORS['text'])
    rounded_box(ax, (0.8, 6.75), 4.0, 1.15,
                'init\nstate = SLEEPING\nparent = 0', COLORS['user_fill'], COLORS['user_edge'], fontsize=10.5)
    rounded_box(ax, (6.0, 6.75), 4.0, 1.15,
                'sh\nstate = SLEEPING\nwait chan = waitchan\nparent = init', COLORS['user_fill'], COLORS['user_edge'], fontsize=10.2)
    rounded_box(ax, (11.2, 6.75), 4.0, 1.15,
                'echo\nstate = RUNNING\nparent = sh\n刚从 exec 返回', COLORS['user_fill'], COLORS['user_edge'], fontsize=10.2)
    seq_arrow(ax, 4.8, 6.0, 7.33, '父子关系', dashed=True, fontsize=9)
    seq_arrow(ax, 10.0, 11.2, 7.33, '父子关系', dashed=True, fontsize=9)

    ax.text(0.6, 6.05, 'echo 地址空间 / 资源快照', fontsize=12.5, fontweight='bold', color=COLORS['text'])
    card_y = 4.15
    w = 2.8
    gap = 0.28
    x0 = 0.8
    cards = [
        ('进程大小', 'sz ≈ 0x4000'),
        ('代码 / 只读段', '.text / .rodata\nLOAD RX: 0 ~ 0x969'),
        ('可写段', 'LOAD RW @ 0x1000\nmemsz = 0x20\nfilesz = 0'),
        ('用户栈', 'top = 0x3fc0\nargv[0] = "echo"\nargv[1] = "hi"\nargv[2] = 0'),
        ('文件描述符', 'fd 0 / 1 / 2 → console file*\n（常见运行时假设）'),
    ]
    xs = []
    for i, (head, body) in enumerate(cards):
        x = x0 + i*(w+gap)
        xs.append(x)
        rounded_box(ax, (x, card_y), w, 1.35, f'{head}\n{body}',
                    COLORS['kernel_fill'], COLORS['kernel_edge'], fontsize=9.4)
    for i in range(len(xs)-1):
        seq_arrow(ax, xs[i]+w, xs[i+1], card_y+0.68, '', dashed=False, fontsize=8)

    rounded_box(ax, (0.8, 1.55), 14.4, 1.55,
                '说明 / 边界条件\n'
                '• fd 0/1/2 指向同一 console file* 是常见运行时假设，不同环境下 ref / inode 细节可能不同。\n'
                '• 若课程要求严格对应单核执行，可注明 CPUS=1，或说明这是“单核等价时刻”的抽象快照。\n'
                '• 若还需展示 p->context / p->trapframe，建议作为独立附图，不要继续塞入主图。',
                COLORS['sched_fill'], COLORS['sched_edge'], fontsize=10.2, ha='left')

    save(fig, 'task1-02-snapshot_standard')


def draw_timer():
    fig, ax = plt.subplots(figsize=(15.5, 11.5))
    ax.set_xlim(0, 15.5)
    ax.set_ylim(0.4, 11.6)
    ax.axis('off')
    ax.text(0.25, 11.35, '任务1时钟中断微观流程（scause = 0x8000000000000005）',
            fontsize=18, fontweight='bold', ha='left', va='top', color=COLORS['text'])
    ax.text(0.25, 10.95, '标准时序版：中断入口、时钟处理、抢占调度、返回用户态依次展开。',
            fontsize=10.5, ha='left', va='top', color='#475569')

    xs = {'U': 1.5, 'TV': 4.6, 'KT': 7.8, 'CLK': 10.7, 'S': 13.8}
    participant_box(ax, xs['U'], '用户态进程\nsh / echo', 'user', y=9.65)
    participant_box(ax, xs['TV'], 'trampoline.uservec', 'kernel', y=9.65)
    participant_box(ax, xs['KT'], 'kerneltrap / devintr', 'kernel', y=9.65)
    participant_box(ax, xs['CLK'], 'Clock / Timer', 'device', y=9.65)
    participant_box(ax, xs['S'], 'scheduler', 'sched', y=9.65)
    for x in xs.values():
        ax.plot([x, x], [1.25, 9.65], color=COLORS['guide'], lw=1.0, ls=(0,(3,4)), zorder=1)

    events = [
        (9.15, 'U','TV','计时器中断到达；scause = 0x8000000000000005', False),
        (8.45, 'TV','TV','保存寄存器到 trapframe；保存 sepc / sstatus', False),
        (7.75, 'TV','KT','切换到内核中断处理路径', False),
        (7.05, 'KT','KT','判断为 supervisor-timer interrupt', False),
        (6.35, 'KT','CLK','devintr() → clockintr()', False),
        (5.65, 'CLK','KT','ticks++；wakeup(&ticks)', True),
        (4.85, 'KT','S','若满足抢占条件：yield() → sched()', False),
        (4.15, 'S','KT','选择并切换到可运行进程', True),
        (3.35, 'KT','TV','prepare_return / userret', False),
        (2.65, 'TV','TV','恢复 trapframe；设置 sepc / sstatus', False),
        (1.95, 'TV','U','sret 返回 U mode', True),
    ]
    for y, a, b, lab, dashed in events:
        seq_arrow(ax, xs[a], xs[b], y, lab, dashed=dashed, fontsize=9.4)

    rounded_box(ax, (2.2, 0.70), 11.3, 1.15,
                '注：若计时器中断恰好发生在 write / read 等系统调用或阻塞路径中，\n'
                '对应的 sleep / wakeup 应放到各自系统调用图中；主时钟图只保留“中断 → 时钟处理 → 抢占 → 返回”的核心链路。',
                '#F8FAFC', '#64748B', fontsize=10.2)

    save(fig, 'task1-03-timer_standard')


if __name__ == '__main__':
    draw_flow()
    draw_snapshot()
    draw_timer()
    print(f'Generated diagrams in: {OUT}')
