// A minimal instance of the editor used by Kick; no account or network access.
const {createEditor,$getRoot,$createParagraphNode}=require('lexical');
const {registerRichText}=require('@lexical/rich-text');
const editor=createEditor({namespace:'kick-fixture',onError:error=>{throw error;}});
editor.setRootElement(document.querySelector('[data-testid=chat-input]'));
registerRichText(editor);
editor.update(()=>{const root=$getRoot();if(!root.getChildrenSize())root.append($createParagraphNode());});
window.kickEditorText=()=>editor.getEditorState().read(()=>$getRoot().getTextContent());
