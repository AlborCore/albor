import unittest

from albor_automation import parse_tree


class ParseTreeChecks(unittest.TestCase):

    def test_parses_role_id_and_name(self) -> None:
        # act
        nodes = parse_tree("button main_ok OK")
        # assert
        self.assertEqual(len(nodes), 1)
        self.assertEqual(nodes[0]["role"], "button")
        self.assertEqual(nodes[0]["id"], "main_ok")
        self.assertEqual(nodes[0]["name"], "OK")

    def test_name_may_contain_spaces(self) -> None:
        # assert
        self.assertEqual(parse_tree("label - No netlist loaded")[0]["name"], "No netlist loaded")

    def test_value_with_spaces_is_read_whole(self) -> None:
        # act
        node = parse_tree("textfield path path value=/Users/foo bar/xyce writable")[0]
        # assert
        self.assertEqual(node["value"], "/Users/foo bar/xyce")
        self.assertTrue(node["writable"])

    def test_state_flags_are_parsed(self) -> None:
        # act
        node = parse_tree("textfield path path value=/bin/echo writable disabled selected focused")[0]
        # assert
        self.assertTrue(node["writable"])
        self.assertTrue(node["disabled"])
        self.assertTrue(node["selected"])
        self.assertTrue(node["focused"])
        self.assertNotIn("read_only", node)

    def test_value_escapes_are_decoded(self) -> None:
        # assert
        self.assertEqual(parse_tree(r"label - name value=hello\nworld")[0]["value"], "hello\nworld")

    def test_name_escapes_are_decoded(self) -> None:
        # assert
        self.assertEqual(parse_tree(r"label - tab\there")[0]["name"], "tab\there")

    def test_literal_backslash_survives_the_round_trip(self) -> None:
        # assert
        self.assertEqual(parse_tree(r"label - name value=C:\\path\\to\\file")[0]["value"], "C:\\path\\to\\file")

    def test_selection_range_is_parsed(self) -> None:
        # assert
        self.assertEqual(parse_tree("textfield path path value=/bin/echo selection=0:4 writable")[0]["selection"], (0, 4))

    def test_blank_id_and_empty_name(self) -> None:
        # act
        node = parse_tree("group - ")[0]
        # assert
        self.assertEqual(node["role"], "group")
        self.assertEqual(node["id"], "-")
        self.assertEqual(node["name"], "")

    def test_multiple_nodes_parse_in_order(self) -> None:
        # act
        nodes = parse_tree("group - \nbutton ok OK\nlabel - Ready")
        # assert
        self.assertEqual([node["role"] for node in nodes], ["group", "button", "label"])
        self.assertEqual([node["name"] for node in nodes], ["", "OK", "Ready"])


if __name__ == "__main__":
    unittest.main()
