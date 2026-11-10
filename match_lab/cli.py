#!/usr/bin/env python3
"""RMUC 2027 rules lab command line.

    python3 match_lab/cli.py example > lab.json      # default configuration
    python3 match_lab/cli.py run [lab.json]          # readable Chinese summary
    python3 match_lab/cli.py run lab.json --json     # full native JSON result
    python3 match_lab/cli.py run lab.json --frames 6 --json > replay.json
    python3 match_lab/cli.py sweep [lab.json] --seeds 20   # same setup, 20 seeds
    python3 match_lab/cli.py build-id

The configuration is validated strictly (exact keys, no duplicates, no
booleans-as-integers) before the native engine is started.
"""
import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from match_lab.build_id import build_id  # noqa: E402
from native_pipe import executable  # noqa: E402

NATIVE = executable(ROOT / 'build' / 'rmuc_match_lab')  # .exe on Windows
CHASSIS = {'hp': 'hp', 'power': 'power'}
LAUNCHER = {'cooling': 'cooling', 'burst': 'burst'}
DART_TARGETS = ('none', 'outpost', 'base-fixed', 'base-random-fixed', 'base-random-moving', 'base-terminal-moving')
TEAM_KEYS = ('infantry', 'sentry', 'max_assembly', 'gold_reserve', 'energy_accuracy', 'dart_target', 'dart_accuracy', 'radar_accuracy')
MAX_SEED = 999_999_999
MAX_SWEEP = 200
DEFAULT = {
    'duration_s': 420,
    'credits': True,
    'seed': 2027,
    'red': {'infantry': [{'chassis': 'hp', 'launcher': 'cooling'}, {'chassis': 'power', 'launcher': 'burst'}],
            'sentry': 'auto', 'max_assembly': 3, 'gold_reserve': 200,
            'energy_accuracy': 70, 'dart_target': 'outpost', 'dart_accuracy': 50, 'radar_accuracy': 70},
    'blue': {'infantry': [{'chassis': 'hp', 'launcher': 'cooling'}, {'chassis': 'power', 'launcher': 'burst'}],
             'sentry': 'auto', 'max_assembly': 3, 'gold_reserve': 200,
             'energy_accuracy': 70, 'dart_target': 'outpost', 'dart_accuracy': 50, 'radar_accuracy': 70},
}


class LabError(ValueError):
    pass


def _no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise LabError(f'duplicate key: {key}')
        out[key] = value
    return out


def _exact(obj, keys, where):
    if not isinstance(obj, dict) or set(obj) != set(keys):
        raise LabError(f'{where} must have exactly the keys {sorted(keys)}')


def _integer(value, lo, hi, where):
    if type(value) is not int or not lo <= value <= hi:
        raise LabError(f'{where} must be an integer in [{lo}, {hi}]')
    return value


def _choice(value, options, where):
    if not isinstance(value, str) or value not in options:
        raise LabError(f'{where} must be one of {sorted(options)}')
    return value


def validate(config):
    _exact(config, ('duration_s', 'credits', 'seed', 'red', 'blue'), 'config')
    _integer(config['duration_s'], 1, 420, 'duration_s')
    _integer(config['seed'], 0, MAX_SEED, 'seed')
    if type(config['credits']) is not bool:
        raise LabError('credits must be a boolean')
    for side in ('red', 'blue'):
        team = config[side]
        _exact(team, TEAM_KEYS, side)
        if not isinstance(team['infantry'], list) or len(team['infantry']) != 2:
            raise LabError(f'{side}.infantry must list exactly 2 infantry robots')
        for i, inf in enumerate(team['infantry']):
            _exact(inf, ('chassis', 'launcher'), f'{side}.infantry[{i}]')
            _choice(inf['chassis'], CHASSIS, f'{side}.infantry[{i}].chassis')
            _choice(inf['launcher'], LAUNCHER, f'{side}.infantry[{i}].launcher')
        _choice(team['sentry'], {'auto', 'semi'}, f'{side}.sentry')
        _integer(team['max_assembly'], 0, 3, f'{side}.max_assembly')
        _integer(team['gold_reserve'], 0, 5000, f'{side}.gold_reserve')
        # Synthetic skill levels in percent; 0 (or dart target "none") leaves the system unused.
        _integer(team['energy_accuracy'], 0, 100, f'{side}.energy_accuracy')
        _choice(team['dart_target'], DART_TARGETS, f'{side}.dart_target')
        _integer(team['dart_accuracy'], 0, 100, f'{side}.dart_accuracy')
        _integer(team['radar_accuracy'], 0, 100, f'{side}.radar_accuracy')
    return config


def spec(team):
    parts = []
    for inf in team['infantry']:
        parts += [inf['chassis'], inf['launcher']]
    parts += [team['sentry'], str(team['max_assembly']), str(team['gold_reserve']), str(team['energy_accuracy']),
              team['dart_target'], str(team['dart_accuracy']), str(team['radar_accuracy'])]
    return ','.join(parts)


def _check_native():
    if not NATIVE.exists():
        raise LabError('native engine missing; run bash tools/build.sh')
    native_build = json.loads(subprocess.run([str(NATIVE), 'BUILD'], capture_output=True, text=True, check=True, timeout=10).stdout)['build']
    if native_build != build_id():
        raise LabError('native engine was built from different sources; run bash tools/build.sh')


def _native_run(config, frames):
    ticks = config['duration_s'] * 60
    args = [str(NATIVE), 'RUN', str(ticks), str(frames), '1' if config['credits'] else '0', str(config['seed']),
            spec(config['red']), spec(config['blue'])]
    out = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if out.returncode != 0:
        raise LabError(out.stdout.strip() or out.stderr.strip())
    return json.loads(out.stdout)


def run(config, frames=0):
    validate(config)
    _check_native()
    result = _native_run(config, frames)
    result['config'] = config
    return result


def sweep(config, seeds):
    """Run the same setup with `seeds` consecutive seeds starting at config['seed'].

    Only the synthetic rolls differ between the matches (dispersion, energy aim,
    dart and radar accuracy). The tallies describe this synthetic model, not real
    win rates.
    """
    validate(config)
    if type(seeds) is not int or not 2 <= seeds <= MAX_SWEEP:
        raise LabError(f'seeds must be an integer in [2, {MAX_SWEEP}]')
    if config['seed'] + seeds - 1 > MAX_SEED:
        raise LabError('seed range exceeds the largest seed')
    _check_native()
    matches = []
    for seed in range(config['seed'], config['seed'] + seeds):
        r = _native_run({**config, 'seed': seed}, 0)
        matches.append({'seed': seed, 'outcome': r['result']['outcome'], 'reason': r['result']['reason'], 'elapsed_ms': r['elapsed_ms'],
                        'hash': r['hash'], 'base_hp': [t['base_hp'] for t in r['teams']],
                        'control_score': [t['control_score'] for t in r['teams']],
                        'attack_damage': [t['attack_damage'] for t in r['teams']]})
    outcomes = {key: sum(m['outcome'] == key for m in matches) for key in ('red-wins', 'blue-wins', 'draw', 'ongoing', 'unresolved')}
    reasons = {}
    for m in matches:
        reasons[m['reason']] = reasons.get(m['reason'], 0) + 1
    return {'schema': 'rmuc2027-rules-lab-sweep.v1', 'official': False, 'build': build_id(), 'config': config, 'seeds': seeds,
            'outcomes': outcomes, 'reasons': reasons,
            'mean': {key: [round(sum(m[key][k] for m in matches) / seeds, 1) for k in range(2)]
                     for key in ('base_hp', 'control_score', 'attack_damage')},
            'matches': matches}


EVENT_LABELS = {
    'death': '战亡', 'revived': '读条复活', 'instant-revival': '兑换立即复活', 'level-up': '升级',
    'purchase': '兑换发弹量', 'remote-heal': '远程兑换血量', 'sentry-supply': '哨兵补给区领弹',
    'assembly': '完成装配', 'heavy-stage': '重装属性阶段', '42mm-unlocked': '解锁42mm', '42mm-installed': '安装42mm',
    '42mm-confirmed': '42mm确认射击', 'temporary-control': '临时控制', 'full-control': '完全控制',
    'outpost-destroyed': '前哨站被击毁', 'outpost-rebuilt': '前哨站重建', 'base-destroyed': '基地被击毁',
    'posture': '哨兵姿态', 'chassis-cutoff': '底盘断电', 'match-start': '比赛开始', 'match-end': '比赛结束',
    'energy-chance': '获得能量机关激活机会', 'energy-trigger': '触发能量机关', 'energy-activated': '激活能量机关',
    'energy-failed': '能量机关激活失败', 'energy-ended': '能量机关增益结束', 'energy-voided': '能量机关增益被清除',
    'energy-suspended': '能量机关增益暂时失效', 'dart-gate': '飞镖闸门', 'dart-launch': '发射飞镖', 'dart-hit': '飞镖命中',
    'dart-miss': '飞镖未命中', 'base-armor': '基地护甲展开', 'radar-marked': '被雷达标记易伤', 'radar-double': '雷达双倍易伤',
}
# Bookkeeping events that would drown the highlights of a summary.
QUIET_EVENTS = ('posture', 'purchase', 'energy-chance', 'dart-gate', 'radar-marked')
OUTCOME = {'red-wins': '红方胜', 'blue-wins': '蓝方胜', 'draw': '平局', 'ongoing': '未结束', 'unresolved': '待裁定'}
REASON = {'base-destroyed': '基地被击毁', 'timeout-control': '超时·控制度', 'timeout-attack-damage': '超时·攻击伤害',
          'timeout-surviving-robot-hp': '超时·剩余血量', 'timeout-equal': '超时·全部相等', 'not-ended': '未到终局'}


def summary(result):
    lines = [f"RMUC 2027 规则全流程实验（非官方，official=false）  build {result['build'][:12]}  seed {result['seed']}",
             f"结果：{OUTCOME.get(result['result']['outcome'], result['result']['outcome'])}"
             f"（{REASON.get(result['result']['reason'], result['result']['reason'])}），"
             f"用时 {result['elapsed_ms'] / 1000:.1f} 秒，状态哈希 {result['hash']}", '']
    lines.append('队伍  基地血量  控制度  攻击伤害  金币(收入/支出)  最高装配  42mm  立即复活  远程回血')
    for t in result['teams']:
        lines.append(f"{'红方' if t['team'] == 'red' else '蓝方'}  {t['base_hp']:>8}  {t['control_score']:>6}  {t['attack_damage']:>8}"
                     f"  {t['gold_income']:>6}/{t['gold_spent']:<6}  {t['highest_assembly']:>8}  {'已装' if t['launcher_installed'] else '未装':>4}"
                     f"  {t['instant_revivals']:>8}  {t['remote_heals']:>8}")
    lines += ['', '阵地            归属   血量  控制方/程度']
    for s in result['structures']:
        lines.append(f"{s['id']:<14}  {s['owner']:<5}{s['hp']:>6}  {s['controller']}/{s['degree']}  转换 {s['transitions']} 次")
    lines += ['', '机器人            等级  血量/上限  击毁  战亡  发射  命中  造成伤害  热量上限/冷却']
    for r in result['robots']:
        lines.append(f"{r['id']:<16}{r['level']:>5}  {r['hp']:>4}/{r['max_hp']:<4}  {r['kills']:>4}  {r['deaths']:>4}  {r['shots']:>4}"
                     f"  {r['hits']:>4}  {r['damage_dealt']:>8}  {r['heat_limit']}/{r['cooling_per_second']}")
    lines += ['', '队伍  能量机关(小/大激活, 重来)  额外经验  飞镖(目标 命中/发射 伤害)  雷达双倍易伤  己方基地护甲']
    for t in result['teams']:
        en, da = t['energy'], t['dart']
        lines.append(f"{'红方' if t['team'] == 'red' else '蓝方'}  {en['small_activations']}/{en['big_activations']}, {en['restarts']:<3}"
                     f"  {en['bonus_experience']:>8}  {da['target']} {da['hits']}/{da['launched']} {da['damage']}"
                     f"  {t['radar']['double_triggers']} 次  {'已展开' if t['base_armor_deployed'] else '闭合'}")
    lines += ['', '关键事件（前 40 条，不含哨兵姿态、兑换、闸门与雷达标记）：']
    shown = 0
    for e in result['events']:
        if e['kind'] in QUIET_EVENTS:
            continue
        lines.append(f"  {e['t'] / 1000:7.2f}s  {EVENT_LABELS.get(e['kind'], e['kind'])}  {e['subject']}  {e['detail']}")
        shown += 1
        if shown >= 40:
            break
    return '\n'.join(lines)


def sweep_summary(result):
    o = result['outcomes']
    n = result['seeds']
    first = result['config']['seed']
    lines = [f"RMUC 2027 规则全流程实验 · 同一配置 {n} 局（seed {first}–{first + n - 1}，非官方）  build {result['build'][:12]}",
             f"红方胜 {o['red-wins']}  蓝方胜 {o['blue-wins']}  平局 {o['draw']}"
             + (f"  未结束 {o['ongoing']}" if o['ongoing'] else '') + (f"  待裁定 {o['unresolved']}" if o['unresolved'] else ''),
             '结束方式：' + '，'.join(f"{REASON.get(k, k)} {v}" for k, v in sorted(result['reasons'].items())),
             f"平均基地血量  红 {result['mean']['base_hp'][0]}  蓝 {result['mean']['base_hp'][1]}",
             f"平均控制度    红 {result['mean']['control_score'][0]}  蓝 {result['mean']['control_score'][1]}",
             f"平均攻击伤害  红 {result['mean']['attack_damage'][0]}  蓝 {result['mean']['attack_damage'][1]}",
             '', '各局只有合成随机数不同（弹道散布、能量机关瞄准、飞镖与雷达准确率）。',
             '场地、机器人物理与 AI 均为合成模型，这些统计不代表真实比赛胜率。']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description='RMUC 2027 rules lab (unofficial)')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('example')
    sub.add_parser('build-id')
    r = sub.add_parser('run')
    r.add_argument('config', nargs='?')
    r.add_argument('--json', action='store_true')
    r.add_argument('--frames', type=int, default=0, help='record robot poses every N ticks (0-600)')
    r.add_argument('--seed', type=int, help='override the configured seed')
    w = sub.add_parser('sweep')
    w.add_argument('config', nargs='?')
    w.add_argument('--seeds', type=int, default=20, help=f'number of consecutive seeds (2-{MAX_SWEEP})')
    w.add_argument('--seed', type=int, help='first seed (default: the configured seed)')
    w.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    if args.command == 'example':
        print(json.dumps(DEFAULT, ensure_ascii=False, indent=2))
        return 0
    if args.command == 'build-id':
        print(build_id())
        return 0
    try:
        config = DEFAULT if not args.config else json.loads(pathlib.Path(args.config).read_text(encoding='utf-8'), object_pairs_hook=_no_duplicates)
        if args.seed is not None:
            if not isinstance(config, dict):
                raise LabError('config must be an object')
            config = {**config, 'seed': args.seed}
        if args.command == 'sweep':
            result = sweep(config, args.seeds)
            print(json.dumps(result, ensure_ascii=False) if args.json else sweep_summary(result))
            return 0
        if not 0 <= args.frames <= 600:
            raise LabError('--frames must be 0..600')
        result = run(config, args.frames)
    except (LabError, OSError, json.JSONDecodeError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False) if args.json else summary(result))
    return 0


if __name__ == '__main__':
    sys.exit(main())
