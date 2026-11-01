"""The framework's components as an app takes them: every component (and the board) has an idf_component.yml, or the
component manager builds the managed copy (a git dependency at a tag) instead of an app's local components/<name>
copy made to try a change (esp32-s3-colormesh v0.1.0-rc.7: its local forge_lvgl was ignored until it had one).

    python -m unittest discover -s tools/harness -p "test_*.py"
"""
import os
import unittest

ESPFORGE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def component_dirs():
    """Every folder with a CMakeLists.txt that registers a component: components/* and boards/*/board."""
    dirs = [os.path.join(ESPFORGE, 'components', d) for d in sorted(os.listdir(os.path.join(ESPFORGE, 'components')))]
    boards = os.path.join(ESPFORGE, 'boards')
    dirs += [os.path.join(boards, b, 'board') for b in sorted(os.listdir(boards))]
    return [d for d in dirs if os.path.isfile(os.path.join(d, 'CMakeLists.txt'))]


class ComponentManifests(unittest.TestCase):
    def test_there_are_components(self):
        self.assertGreaterEqual(len(component_dirs()), 9)

    def test_every_component_has_a_manifest(self):
        missing = [os.path.relpath(d, ESPFORGE) for d in component_dirs()
                   if not os.path.isfile(os.path.join(d, 'idf_component.yml'))]
        self.assertEqual(missing, [], 'components without idf_component.yml (an app could not override them)')

    def test_manifests_declare_dependencies(self):
        for d in component_dirs():
            with open(os.path.join(d, 'idf_component.yml'), encoding='utf-8') as f:
                text = f.read()
            self.assertIn('dependencies:', text, os.path.relpath(d, ESPFORGE))


if __name__ == '__main__':
    unittest.main()
